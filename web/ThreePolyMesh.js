// Three.js translation of src/RendComp/PolyMesh Mesh.cpp + polymesh.cpp.
//
// This module intentionally leaves out the original engine systems:
// physics/collision, continuous LOD mutation, light-manager vertex relighting,
// render-pipeline state fallbacks, specular highlight-map passes, UV animation,
// archetype/instance handle maps, and mesh splitting.
//
// Input is expected to be an already decoded Mesh/XMesh-shaped object with the
// same logical arrays as Include/Mesh.h:
// - objectVertexList: [{x,y,z}], unique object-space positions
// - textureVertexList: [{u,v}], unique texture coordinates
// - vertexBatchList: indices into objectVertexList
// - textureBatchList: indices into textureVertexList
// - textureBatchList2: optional second UV indices
// - normalABC: [{x,y,z}], unique normals
// - vertexNormal: per object vertex index into normalABC
// - faceGroups: [{material, faceCnt, faceVertexChain, faceNormal, faceProperties}]
// - materialList: material records matching Include/material.h
//
// The original render path walks each face group, skips HIDDEN faces, culls
// single-sided backfaces at draw time, expands face vertices through the batch
// lists, chooses face normals for FLAT_SHADED faces and vertex normals for
// smooth faces, then renders each face group with its material and texture
// passes. Three.js handles frustum culling and draw submission, so this file
// builds static BufferGeometry per face group and assigns one material per group.

import * as THREE from 'three';

export const FaceProperty = Object.freeze({
  TWO_SIDED: 0x01,
  FLAT_SHADED: 0x02,
  SMOOTH_SHADED: 0x04,
  HIDDEN: 0x08,
});

export const MaterialFlag = Object.freeze({
  WHITE: 0x0001,
  EMITTER: 0x0002,
  AMBIENT: 0x0004,
  DIFFUSE: 0x0008,
  SPECULAR: 0x0010,
  NO_DIFFUSE1_PASS: 0x0080,
  NO_DIFFUSE2_PASS: 0x0100,
  NO_EMITTER_PASS: 0x0200,
  NO_SPECULAR_PASS: 0x0400,
  NO_LIGHTING_PASS: 0x0800,
  ENABLE_DEPTH_WRITES_ALWAYS: 0x1000,
  ENABLE_ALPHA_BLEND_NEVER: 0x2000,
});

export const TextureFlag = Object.freeze({
  HAS_ALPHA: 0x80000000,
});

export const TextureAddressMode = Object.freeze({
  REPEAT: 0,
  MIRROR: 1,
  CLAMP: 2,
  BORDER: 3,
});

export const TextureWrapMode = Object.freeze({
  PLANAR: 0,
  CYL_U: 1,
  CYL_V: 2,
  SPHERICAL: 3,
  UV_0: 4,
  UV_1: 5,
});

const DEFAULT_COLOR = Object.freeze({ r: 255, g: 255, b: 255 });
const DEFAULT_OPTIONS = Object.freeze({
  includeHiddenFaces: false,
  flipV: false,
  flipZ: false,
  materialType: 'phong',
  vertexColors: true,
  textureColorSpace: 'srgb',
  textureResolver: null,
});

export function createThreePolyMesh(mesh, options = {}) {
  const opts = { ...DEFAULT_OPTIONS, ...options };
  const group = new THREE.Group();

  group.name = mesh.name || 'PolyMesh';
  group.userData.source = 'Conquest Frontier Wars PolyMesh';
  group.userData.bounds = normalizeBounds(mesh.bounds);
  group.userData.sphere = normalizeSphere(mesh);

  const faceGroups = getArray(mesh.faceGroups, mesh.face_groups);
  const materialList = getArray(mesh.materialList, mesh.material_list);

  for (let groupIndex = 0; groupIndex < faceGroups.length; groupIndex += 1) {
    const faceGroup = faceGroups[groupIndex];
    const geometry = createFaceGroupGeometry(mesh, faceGroup, opts);

    if (geometry.getAttribute('position').count === 0) {
      geometry.dispose();
      continue;
    }

    const sourceMaterial = materialList[read(faceGroup, 'material', -1)] || null;
    const materialPasses = createThreeMaterialPasses(sourceMaterial, opts);
    const side = isFaceGroupDoubleSided(faceGroup) ? THREE.DoubleSide : THREE.FrontSide;

    const threeMesh = new THREE.Mesh(geometry, materialPasses[0]);

    threeMesh.name = sourceMaterial?.name || `FaceGroup_${groupIndex}`;
    threeMesh.userData.faceGroupIndex = groupIndex;
    threeMesh.userData.materialIndex = read(faceGroup, 'material', -1);
    threeMesh.userData.materialPass = 'base';
    threeMesh.material.side = side;
    group.add(threeMesh);

    for (let passIndex = 1; passIndex < materialPasses.length; passIndex += 1) {
      const passMesh = new THREE.Mesh(geometry.clone(), materialPasses[passIndex]);
      passMesh.name = `${threeMesh.name}_${materialPasses[passIndex].userData.passName || `pass${passIndex}`}`;
      passMesh.userData.faceGroupIndex = groupIndex;
      passMesh.userData.materialIndex = read(faceGroup, 'material', -1);
      passMesh.userData.materialPass = materialPasses[passIndex].userData.passName || `pass${passIndex}`;
      passMesh.material.side = side;
      passMesh.renderOrder = passIndex;
      group.add(passMesh);
    }
  }

  return group;
}

export function createFaceGroupGeometry(mesh, faceGroup, options = {}) {
  const opts = { ...DEFAULT_OPTIONS, ...options };
  const objectVertices = getArray(mesh.objectVertexList, mesh.object_vertex_list);
  const textureVertices = getArray(mesh.textureVertexList, mesh.texture_vertex_list);
  const vertexBatch = getArray(mesh.vertexBatchList, mesh.vertex_batch_list);
  const textureBatch = getArray(mesh.textureBatchList, mesh.texture_batch_list);
  const textureBatch2 = getArray(mesh.textureBatchList2, mesh.texture_batch_list2);
  const normals = getArray(mesh.normalABC, mesh.normal_ABC);
  const vertexNormals = getArray(mesh.vertexNormal, mesh.vertex_normal);
  const vertexColors = getArray(mesh.vertexColorList, mesh.vertex_color_list);
  const materialList = getArray(mesh.materialList, mesh.material_list);
  const material = materialList[read(faceGroup, 'material', -1)] || null;

  const faceCount = read(faceGroup, 'faceCnt', read(faceGroup, 'face_cnt', 0));
  const faceChain = getArray(faceGroup.faceVertexChain, faceGroup.face_vertex_chain);
  const faceNormals = getArray(faceGroup.faceNormal, faceGroup.face_normal);
  const faceProperties = getArray(faceGroup.faceProperties, faceGroup.face_properties);

  const positions = [];
  const normalValues = [];
  const uvs = [];
  const uv2s = [];
  const colors = [];

  for (let faceIndex = 0; faceIndex < faceCount; faceIndex += 1) {
    const properties = faceProperties[faceIndex] ?? FaceProperty.SMOOTH_SHADED;

    if (!opts.includeHiddenFaces && (properties & FaceProperty.HIDDEN)) {
      continue;
    }

    const flat = Boolean(properties & FaceProperty.FLAT_SHADED);
    const faceNormal = vectorAt(normals, faceNormals[faceIndex]);

    for (let vertexInFace = 0; vertexInFace < 3; vertexInFace += 1) {
      const batchIndex = faceChain[faceIndex * 3 + vertexInFace];
      const objectVertexIndex = vertexBatch[batchIndex];
      const textureVertexIndex = textureBatch[batchIndex];
      const secondTextureVertexIndex = textureBatch2.length ? textureBatch2[batchIndex] : textureVertexIndex;

      const position = vectorAt(objectVertices, objectVertexIndex);
      const normalIndex = vertexNormals[objectVertexIndex];
      const normal = flat ? faceNormal : vectorAt(normals, normalIndex);
      const uv = texCoordAt(textureVertices, textureVertexIndex, opts.flipV);
      const uv2 = texCoordAt(textureVertices, secondTextureVertexIndex, opts.flipV);
      const color = computeVertexColor(material, vertexColors, objectVertexIndex);

      pushVector3(positions, position, opts.flipZ);
      pushVector3(normalValues, normal, opts.flipZ);
      uvs.push(uv.u, uv.v);
      uv2s.push(uv2.u, uv2.v);
      colors.push(color.r, color.g, color.b);
    }
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute('normal', new THREE.Float32BufferAttribute(normalValues, 3));
  geometry.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));

  if (uv2s.length > 0) {
    geometry.setAttribute('uv2', new THREE.Float32BufferAttribute(uv2s, 2));
  }

  if (opts.vertexColors) {
    geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
  }

  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();

  if (!normalValues.some((value) => value !== 0)) {
    geometry.computeVertexNormals();
  }

  return geometry;
}

export function createThreeMaterialPasses(material, options = {}) {
  const opts = { ...DEFAULT_OPTIONS, ...options };
  const passes = [createThreeMaterial(material, opts)];
  const secondDiffuseMap = resolveTexture(material, 'secondDiffuse', opts);
  const emissiveMap = resolveTexture(material, 'emissive', opts);

  if (secondDiffuseMap && !hasFlag(material, MaterialFlag.NO_DIFFUSE2_PASS)) {
    passes.push(createTexturePassMaterial(material, secondDiffuseMap, 'secondDiffuse', THREE.MultiplyBlending, opts));
  }

  if (emissiveMap && !hasFlag(material, MaterialFlag.NO_EMITTER_PASS)) {
    passes.push(createTexturePassMaterial(material, emissiveMap, 'emissive', THREE.AdditiveBlending, opts));
  }

  return passes;
}

export function createThreeMaterial(material, options = {}) {
  const opts = { ...DEFAULT_OPTIONS, ...options };
  const diffuse = normalizeRgb(material?.diffuse, DEFAULT_COLOR);
  const emission = normalizeRgb(material?.emission, { r: 0, g: 0, b: 0 });
  const specular = normalizeRgb(material?.specular, { r: 0, g: 0, b: 0 });
  const opacity = (material?.transparency ?? 255) / 255;
  const map = resolveTexture(material, 'diffuse', opts);
  const transparent =
    opacity < 1 ||
    Boolean((material?.textureFlags ?? material?.texture_flags ?? 0) & TextureFlag.HAS_ALPHA);
  const baseColor = opts.vertexColors ? DEFAULT_COLOR : diffuse;

  const params = {
    name: material?.name || 'PolyMeshMaterial',
    color: toThreeColor(baseColor),
    opacity,
    transparent: transparent && !hasFlag(material, MaterialFlag.ENABLE_ALPHA_BLEND_NEVER),
    depthWrite: opacity >= 1 || hasFlag(material, MaterialFlag.ENABLE_DEPTH_WRITES_ALWAYS),
    side: THREE.FrontSide,
    vertexColors: opts.vertexColors,
    map,
  };

  if (opts.materialType === 'basic' || hasFlag(material, MaterialFlag.NO_LIGHTING_PASS)) {
    params.combine = THREE.MultiplyOperation;
    return new THREE.MeshBasicMaterial(params);
  }

  params.emissive = toThreeColor(emission);
  params.specular = toThreeColor(specular);
  params.shininess = Math.max(0, (material?.shininessWidth ?? material?.shininess_width ?? 0) * 32);

  return new THREE.MeshPhongMaterial(params);
}

export function applyFaceGroupSide(group, mesh) {
  const faceGroups = getArray(mesh.faceGroups, mesh.face_groups);

  group.children.forEach((child) => {
    const faceGroup = faceGroups[child.userData.faceGroupIndex];
    child.material.side = isFaceGroupDoubleSided(faceGroup) ? THREE.DoubleSide : THREE.FrontSide;
    child.material.needsUpdate = true;
  });
}

export function getTextureWrapMode(textureFlags = 0) {
  return (textureFlags & 0xf0) >> 4;
}

export function getTextureAddressMode(textureFlags = 0, coord = 0) {
  return coord === 0 ? textureFlags & 0x03 : (textureFlags & 0x0c) >> 2;
}

function computeVertexColor(material, vertexColors, objectVertexIndex) {
  const diffuse = normalizeRgb(material?.diffuse, DEFAULT_COLOR);
  const emission = normalizeRgb(material?.emission, { r: 0, g: 0, b: 0 });

  let r = diffuse.r + emission.r;
  let g = diffuse.g + emission.g;
  let b = diffuse.b + emission.b;

  if (vertexColors.length >= objectVertexIndex * 3 + 3) {
    r = (r * vertexColors[objectVertexIndex * 3]) / 255;
    g = (g * vertexColors[objectVertexIndex * 3 + 1]) / 255;
    b = (b * vertexColors[objectVertexIndex * 3 + 2]) / 255;
  }

  return {
    r: Math.min(1, r / 255),
    g: Math.min(1, g / 255),
    b: Math.min(1, b / 255),
  };
}

function resolveTexture(material, slot, options) {
  if (!material || typeof options.textureResolver !== 'function') {
    return null;
  }

  const texture = options.textureResolver(material, slot);

  if (texture && options.textureColorSpace === 'srgb' && 'colorSpace' in texture) {
    texture.colorSpace = THREE.SRGBColorSpace;
  }

  if (texture) {
    const flags = textureFlagsForSlot(material, slot);
    texture.wrapS = threeWrapMode(getTextureAddressMode(flags, 0));
    texture.wrapT = threeWrapMode(getTextureAddressMode(flags, 1));
    texture.needsUpdate = true;
  }

  return texture || null;
}

function isFaceGroupDoubleSided(faceGroup) {
  const props = getArray(faceGroup?.faceProperties, faceGroup?.face_properties);
  return props.some((property) => property & FaceProperty.TWO_SIDED);
}

function createTexturePassMaterial(material, map, passName, blending, options) {
  const opacity =
    passName === 'emissive'
      ? (material?.emissiveBlend ?? material?.emissive_blend ?? 255) / 255
      : 1;

  const passMaterial = new THREE.MeshBasicMaterial({
    name: `${material?.name || 'PolyMeshMaterial'}_${passName}`,
    color: 0xffffff,
    opacity,
    transparent: true,
    depthWrite: false,
    depthTest: true,
    side: THREE.FrontSide,
    map,
    blending,
    vertexColors: options.vertexColors,
  });

  passMaterial.userData.passName = passName;
  return passMaterial;
}

function textureFlagsForSlot(material, slot) {
  if (slot === 'secondDiffuse') {
    return material?.secondDiffuseTextureFlags ?? material?.second_diffuse_texture_flags ?? 0;
  }

  if (slot === 'emissive') {
    return material?.emissiveTextureFlags ?? material?.emissive_texture_flags ?? 0;
  }

  return material?.textureFlags ?? material?.texture_flags ?? 0;
}

function threeWrapMode(addressMode) {
  switch (addressMode) {
    case TextureAddressMode.MIRROR:
      return THREE.MirroredRepeatWrapping;
    case TextureAddressMode.CLAMP:
    case TextureAddressMode.BORDER:
      return THREE.ClampToEdgeWrapping;
    case TextureAddressMode.REPEAT:
    default:
      return THREE.RepeatWrapping;
  }
}

function hasFlag(material, flag) {
  return Boolean((material?.flags ?? 0) & flag);
}

function normalizeRgb(value, fallback) {
  const rgb = value || fallback;
  return {
    r: clamp255(rgb.r ?? rgb[0] ?? fallback.r),
    g: clamp255(rgb.g ?? rgb[1] ?? fallback.g),
    b: clamp255(rgb.b ?? rgb[2] ?? fallback.b),
  };
}

function toThreeColor(rgb) {
  return new THREE.Color(rgb.r / 255, rgb.g / 255, rgb.b / 255);
}

function normalizeBounds(bounds) {
  if (!bounds) {
    return null;
  }

  return {
    maxX: bounds[0],
    minX: bounds[1],
    maxY: bounds[2],
    minY: bounds[3],
    maxZ: bounds[4],
    minZ: bounds[5],
  };
}

function normalizeSphere(mesh) {
  const center = mesh.sphereCenter || mesh.sphere_center;

  if (!center && mesh.radius == null) {
    return null;
  }

  return {
    center: center ? { x: center.x, y: center.y, z: center.z } : { x: 0, y: 0, z: 0 },
    radius: mesh.radius ?? 0,
  };
}

function vectorAt(values, index) {
  const value = values[index] || {};
  return {
    x: value.x ?? value[0] ?? 0,
    y: value.y ?? value[1] ?? 0,
    z: value.z ?? value[2] ?? 0,
  };
}

function texCoordAt(values, index, flipV) {
  const value = values[index] || {};
  const v = value.v ?? value[1] ?? 0;

  return {
    u: value.u ?? value[0] ?? 0,
    v: flipV ? 1 - v : v,
  };
}

function pushVector3(target, value, flipZ) {
  target.push(value.x, value.y, flipZ ? -value.z : value.z);
}

function getArray(...candidates) {
  for (const candidate of candidates) {
    if (Array.isArray(candidate) || ArrayBuffer.isView(candidate)) {
      return candidate;
    }
  }

  return [];
}

function read(object, key, fallback) {
  return object && object[key] != null ? object[key] : fallback;
}

function clamp255(value) {
  return Math.max(0, Math.min(255, value));
}
