import * as THREE from 'three';
import { loadUtfFile } from './utfParser.js';
import { createThreePolyMesh } from './ThreePolyMesh.js';

const fileInput = document.querySelector('#utf-file');
const openButton = document.querySelector('#open-file');
const resetButton = document.querySelector('#reset-view');
const wireframeToggle = document.querySelector('#wireframe');
const statusLine = document.querySelector('#status');
const statsLine = document.querySelector('#stats');
const canvas = document.querySelector('#viewport');

const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x15171b);

const camera = new THREE.PerspectiveCamera(60, 1, 0.1, 100000);
const cameraTarget = new THREE.Vector3();
const orbit = {
  yaw: Math.PI / 4,
  pitch: Math.PI / 6,
  distance: 10,
};

let activeMesh = null;
let activeTextureCanvases = new Map();
let animationFrame = 0;

scene.add(new THREE.HemisphereLight(0xffffff, 0x273044, 1.4));

const keyLight = new THREE.DirectionalLight(0xffffff, 1.8);
keyLight.position.set(8, 12, 10);
scene.add(keyLight);

const fillLight = new THREE.DirectionalLight(0x9fb7ff, 0.65);
fillLight.position.set(-8, 4, -6);
scene.add(fillLight);

const grid = new THREE.GridHelper(20, 20, 0x46505f, 0x2a303a);
grid.visible = true;
scene.add(grid);

const axes = new THREE.AxesHelper(3);
scene.add(axes);

openButton.addEventListener('click', () => fileInput.click());
resetButton.addEventListener('click', frameActiveMesh);
wireframeToggle.addEventListener('change', () => setWireframe(wireframeToggle.checked));
fileInput.addEventListener('change', onFileSelected);
window.addEventListener('resize', resizeRenderer);

installPointerControls(canvas);
resizeRenderer();
animate();

async function onFileSelected(event) {
  const [file] = event.target.files || [];
  if (!file) {
    return;
  }

  setStatus(`Loading ${file.name}...`);

  try {
    const utfRoot = await loadUtfFile(file);
    const rootNode = findUtfRootNode(utfRoot);

    if (!rootNode?.children) {
      throw new Error('UTF root has no children.');
    }

    let textureCanvases = loadTextureCanvases(rootNode.children);
    const meshEntries = findPolyMeshEntries(rootNode);
    let threeMesh;
    let stats;

    if (meshEntries.length > 0) {
      const compound = buildCompoundObject(rootNode, meshEntries, textureCanvases);
      threeMesh = compound.object;
      textureCanvases = compound.textureCanvases;
      stats = compound.stats;
    } else if (rootNode.children.Faces && rootNode.children.Vertices) {
      const shieldMesh = buildShieldMesh(rootNode.children, file.name);
      threeMesh = shieldMesh.object;
      stats = shieldMesh.stats;
    } else {
      throw new Error('No renderable PolyMesh or old shield Faces/Vertices data found.');
    }

    setActiveMesh(threeMesh, textureCanvases);
    setWireframe(wireframeToggle.checked);
    frameActiveMesh();
    updateStats(stats, textureCanvases);
    setStatus(`Loaded ${file.name}`);
  } catch (error) {
    console.error(error);
    setStatus(error instanceof Error ? error.message : String(error), true);
  } finally {
    event.target.value = '';
  }
}

function buildPolyMeshData(root, textureCanvases, name) {
  const verticesNode = root['Vertices']?.children;
  const normalsNode = root['Normals']?.children;
  const faceGroupsNode = root['Face groups']?.children;

  if (!verticesNode || !faceGroupsNode) {
    throw new Error('Mesh is missing Vertices or Face groups data.');
  }

  const materialResult = loadMaterials(root['Material library']?.children || {}, textureCanvases);
  const objectVertexList = vector3Array(getFloatArray(verticesNode['Object vertex list']));
  const textureVertexList = uvArray(getFloatArray(verticesNode['Texture vertex list']));
  const normalABC = vector3Array(getFloatArray(normalsNode?.['Surface normal list']));
  const vertexBatchList = Array.from(getIntArray(verticesNode['Vertex batch list']) || []);
  const textureBatchList = Array.from(getIntArray(verticesNode['Texture batch list']) || []);
  const textureBatchList2 = Array.from(getIntArray(verticesNode['Texture batch list2']) || []);
  const vertexNormal = Array.from(getIntArray(verticesNode['Vertex normal']) || []);
  const vertexColorList = verticesNode['Color']?.value || [];
  const faceGroups = [];

  for (const key of Object.keys(faceGroupsNode).filter((item) => item.startsWith('Group')).sort(naturalSort)) {
    const group = faceGroupsNode[key].children;
    if (!group) {
      continue;
    }

    const materialId = getInt32(group['Material']) ?? 0;
    const materialIndex = materialResult.idToIndex.get(materialId) ?? materialId;
    const chain = getIntArray(group['Face vertex chain']);
    const faceNormal = getIntArray(group['Face normal']);
    const faceProperties = getIntArray(group['Face property']);
    const faceCount = getInt32(group['Face count']) ?? Math.floor((chain?.length || 0) / 3);

    if (!chain) {
      continue;
    }

    faceGroups.push({
      material: materialIndex,
      faceCnt: faceCount,
      faceVertexChain: Array.from(chain),
      faceNormal: Array.from(faceNormal || []),
      faceProperties: Array.from(faceProperties || []),
    });
  }

  if (objectVertexList.length === 0 || faceGroups.length === 0) {
    throw new Error('Mesh has no renderable vertices or face groups.');
  }

  return {
    name,
    objectVertexList,
    textureVertexList,
    vertexBatchList,
    textureBatchList,
    textureBatchList2,
    normalABC,
    vertexNormal,
    vertexColorList,
    faceGroups,
    materialList: materialResult.materials,
  };
}

function buildCompoundObject(rootNode, meshEntries, textureCanvases) {
  const object = new THREE.Group();
  const partInfos = readCompoundParts(rootNode.children?.Cmpnd);
  const joints = readCompoundJoints(rootNode.children?.Cmpnd?.children?.Cons);
  const byFileName = new Map(meshEntries.map((entry) => [entry.containerNode.name.toLowerCase(), entry]));
  const byPartName = new Map();
  const stats = { vertices: 0, faces: 0, materials: 0 };
  let mergedTextures = textureCanvases;

  object.name = rootNode.name || 'Compound';

  if (partInfos.length === 0) {
    for (const entry of meshEntries) {
      const partTextures = mergeTextureCanvases(mergedTextures, loadTextureCanvases(entry.meshNode.children));
      const meshData = buildPolyMeshData(entry.meshNode.children, partTextures, entry.name);
      const meshGroup = createThreePolyMesh(meshData, {
        flipV: true,
        vertexColors: true,
        textureResolver: createTextureResolver(partTextures),
      });

      meshGroup.name = entry.name;
      object.add(meshGroup);
      mergedTextures = mergeTextureCanvases(mergedTextures, partTextures);
      stats.vertices += meshData.objectVertexList.length;
      stats.faces += meshData.faceGroups.reduce((total, group) => total + group.faceCnt, 0);
      stats.materials += meshData.materialList.length;
    }

    return { object, textureCanvases: mergedTextures, stats };
  }

  for (const part of partInfos) {
    const entry = byFileName.get(part.fileName.toLowerCase());
    if (!entry) {
      continue;
    }

    const partTextures = mergeTextureCanvases(mergedTextures, loadTextureCanvases(entry.meshNode.children));
    const meshData = buildPolyMeshData(entry.meshNode.children, partTextures, part.objectName);
    const meshGroup = createThreePolyMesh(meshData, {
      flipV: true,
      vertexColors: true,
      textureResolver: createTextureResolver(partTextures),
    });

    meshGroup.name = part.objectName;
    meshGroup.userData.fileName = part.fileName;
    meshGroup.userData.partName = part.objectName;
    byPartName.set(part.objectName, meshGroup);
    mergedTextures = mergeTextureCanvases(mergedTextures, partTextures);
    stats.vertices += meshData.objectVertexList.length;
    stats.faces += meshData.faceGroups.reduce((total, group) => total + group.faceCnt, 0);
    stats.materials += meshData.materialList.length;
  }

  const childrenByParent = new Map();
  for (const joint of joints) {
    if (!childrenByParent.has(joint.parent)) {
      childrenByParent.set(joint.parent, []);
    }
    childrenByParent.get(joint.parent).push(joint);
  }

  const attached = new Set();
  const attachPart = (partName, parentObject) => {
    const partObject = byPartName.get(partName);
    if (!partObject || attached.has(partName)) {
      return;
    }

    attached.add(partName);
    parentObject.add(partObject);

    for (const joint of childrenByParent.get(partName) || []) {
      const childObject = byPartName.get(joint.child);
      if (!childObject) {
        continue;
      }

      applyJointLocalTransform(childObject, joint);
      attachPart(joint.child, partObject);
    }
  };

  attachPart('Root', object);

  for (const [partName, partObject] of byPartName) {
    if (!attached.has(partName)) {
      object.add(partObject);
    }
  }

  object.userData.compoundJoints = joints;
  return { object, textureCanvases: mergedTextures, stats };
}

function readCompoundParts(cmpndNode) {
  if (!cmpndNode?.children) {
    return [];
  }

  return Object.values(cmpndNode.children)
    .filter((node) => node.children && (node.name === 'Root' || node.name.startsWith('Part')))
    .map((node) => ({
      partDir: node.name,
      objectName: getString(node.children['Object name']) || node.name,
      fileName: getString(node.children['File name']) || '',
      index: getInt32(node.children.Index) ?? -1,
    }))
    .filter((part) => part.fileName);
}

function readCompoundJoints(consNode) {
  if (!consNode?.children) {
    return [];
  }

  return [
    ...readFixedLikeJoints(consNode.children.Fix, 'fixed'),
    ...readFixedLikeJoints(consNode.children.Trans, 'translational'),
    ...readFixedLikeJoints(consNode.children.Loose, 'loose'),
    ...readRevLikeJoints(consNode.children.Rev, 'revolute'),
    ...readRevLikeJoints(consNode.children.Pris, 'prismatic'),
    ...readSphereJoints(consNode.children.Sphere),
  ];
}

function readFixedLikeJoints(node, type) {
  if (!node?.value) {
    return [];
  }

  const recordSize = 64 + 64 + 12 + 36;
  const view = new DataView(node.value.buffer, node.value.byteOffset, node.value.byteLength);
  const out = [];

  for (let offset = 0; offset + recordSize <= view.byteLength; offset += recordSize) {
    out.push({
      type,
      parent: readFixedString(view, offset, 64),
      child: readFixedString(view, offset + 64, 64),
      relPosition: readVector3(view, offset + 128),
      relOrientation: readMatrix3(view, offset + 140),
      parentPoint: new THREE.Vector3(),
      childPoint: new THREE.Vector3(),
      axis: new THREE.Vector3(),
      min0: 0,
      max0: 0,
    });
  }

  return out;
}

function readRevLikeJoints(node, type) {
  if (!node?.value) {
    return [];
  }

  const recordSize = 64 + 64 + 12 + 12 + 36 + 12 + 4 + 4;
  const view = new DataView(node.value.buffer, node.value.byteOffset, node.value.byteLength);
  const out = [];

  for (let offset = 0; offset + recordSize <= view.byteLength; offset += recordSize) {
    out.push({
      type,
      parent: readFixedString(view, offset, 64),
      child: readFixedString(view, offset + 64, 64),
      parentPoint: readVector3(view, offset + 128),
      childPoint: readVector3(view, offset + 140),
      relOrientation: readMatrix3(view, offset + 152),
      axis: readVector3(view, offset + 188),
      min0: view.getFloat32(offset + 200, true),
      max0: view.getFloat32(offset + 204, true),
    });
  }

  return out;
}

function readSphereJoints(node) {
  if (!node?.value) {
    return [];
  }

  const recordSize = 64 + 64 + 12 + 12 + 36 + 24;
  const view = new DataView(node.value.buffer, node.value.byteOffset, node.value.byteLength);
  const out = [];

  for (let offset = 0; offset + recordSize <= view.byteLength; offset += recordSize) {
    out.push({
      type: 'spherical',
      parent: readFixedString(view, offset, 64),
      child: readFixedString(view, offset + 64, 64),
      parentPoint: readVector3(view, offset + 128),
      childPoint: readVector3(view, offset + 140),
      relOrientation: readMatrix3(view, offset + 152),
      axis: new THREE.Vector3(),
      min0: view.getFloat32(offset + 188, true),
      max0: view.getFloat32(offset + 192, true),
      min1: view.getFloat32(offset + 196, true),
      max1: view.getFloat32(offset + 200, true),
      min2: view.getFloat32(offset + 204, true),
      max2: view.getFloat32(offset + 208, true),
    });
  }

  return out;
}

function applyJointLocalTransform(object, joint) {
  if (joint.type === 'fixed' || joint.type === 'translational' || joint.type === 'loose') {
    object.matrix.copy(matrix4FromRotationTranslation(joint.relOrientation, joint.relPosition));
  } else {
    const rotation = matrix4FromMatrix3(joint.relOrientation);
    const childPoint = joint.childPoint.clone().applyMatrix4(rotation);
    const translation = joint.parentPoint.clone().sub(childPoint);
    object.matrix.copy(matrix4FromRotationTranslation(joint.relOrientation, translation));
  }

  object.matrixAutoUpdate = false;
  object.matrixWorldNeedsUpdate = true;
}

function readFixedString(view, offset, maxLength) {
  const bytes = new Uint8Array(view.buffer, view.byteOffset + offset, maxLength);
  let end = 0;

  while (end < bytes.length && bytes[end] !== 0) {
    end += 1;
  }

  return new TextDecoder('ascii').decode(bytes.slice(0, end));
}

function readVector3(view, offset) {
  return new THREE.Vector3(
    view.getFloat32(offset, true),
    view.getFloat32(offset + 4, true),
    view.getFloat32(offset + 8, true),
  );
}

function readMatrix3(view, offset) {
  return {
    e00: view.getFloat32(offset, true),
    e01: view.getFloat32(offset + 4, true),
    e02: view.getFloat32(offset + 8, true),
    e10: view.getFloat32(offset + 12, true),
    e11: view.getFloat32(offset + 16, true),
    e12: view.getFloat32(offset + 20, true),
    e20: view.getFloat32(offset + 24, true),
    e21: view.getFloat32(offset + 28, true),
    e22: view.getFloat32(offset + 32, true),
  };
}

function matrix4FromMatrix3(matrix) {
  return new THREE.Matrix4().set(
    matrix.e00, matrix.e01, matrix.e02, 0,
    matrix.e10, matrix.e11, matrix.e12, 0,
    matrix.e20, matrix.e21, matrix.e22, 0,
    0, 0, 0, 1,
  );
}

function matrix4FromRotationTranslation(matrix, translation) {
  return new THREE.Matrix4().set(
    matrix.e00, matrix.e01, matrix.e02, translation.x,
    matrix.e10, matrix.e11, matrix.e12, translation.y,
    matrix.e20, matrix.e21, matrix.e22, translation.z,
    0, 0, 0, 1,
  );
}

function buildShieldMesh(root, name) {
  const faceBytes = root.Faces?.value;
  const vertexBytes = root.Vertices?.value;

  if (!faceBytes || !vertexBytes || faceBytes.byteLength < 2 || vertexBytes.byteLength < 2) {
    throw new Error('Shield mesh is missing Faces or Vertices data.');
  }

  const faceView = new DataView(faceBytes.buffer, faceBytes.byteOffset, faceBytes.byteLength);
  const vertexView = new DataView(vertexBytes.buffer, vertexBytes.byteOffset, vertexBytes.byteLength);
  const faceCount = faceView.getUint16(0, true);
  const vertexCount = vertexView.getUint16(0, true);
  const positions = [];
  const normals = [];
  const indices = [];

  for (let i = 0; i < vertexCount; i += 1) {
    const offset = 2 + i * 24;
    positions.push(
      vertexView.getFloat32(offset, true),
      vertexView.getFloat32(offset + 4, true),
      vertexView.getFloat32(offset + 8, true),
    );
    normals.push(
      vertexView.getFloat32(offset + 12, true),
      vertexView.getFloat32(offset + 16, true),
      vertexView.getFloat32(offset + 20, true),
    );
  }

  for (let i = 0; i < faceCount; i += 1) {
    const offset = 2 + i * 20;
    indices.push(
      faceView.getUint16(offset + 12, true),
      faceView.getUint16(offset + 14, true),
      faceView.getUint16(offset + 16, true),
    );
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
  geometry.setIndex(indices);
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();

  const material = new THREE.MeshPhongMaterial({
    name: 'Shield',
    color: 0x6fa9ff,
    emissive: 0x173052,
    transparent: true,
    opacity: 0.36,
    side: THREE.DoubleSide,
    depthWrite: false,
  });
  const mesh = new THREE.Mesh(geometry, material);
  mesh.name = name;

  return {
    object: mesh,
    stats: {
      vertices: vertexCount,
      faces: faceCount,
      materials: 1,
    },
  };
}

function loadMaterials(materialsNode, textureCanvases) {
  const materials = [];
  const idToIndex = new Map();
  const materialKeys = Object.keys(materialsNode)
    .filter((key) => key !== 'Material count')
    .filter((key) => materialsNode[key]?.children)
    .sort(naturalSort);

  materialKeys.forEach((key, index) => {
    const materialNode = materialsNode[key];
    const material = readMaterial(materialNode, textureCanvases);
    const materialId = getInt32(materialNode.children?.['Material identifier']) ?? index;

    idToIndex.set(materialId, materials.length);
    materials.push(material);
  });

  if (materials.length === 0) {
    materials.push({
      name: 'Default',
      flags: 0x0008,
      diffuse: { r: 255, g: 255, b: 255 },
      emission: { r: 0, g: 0, b: 0 },
      specular: { r: 25, g: 25, b: 25 },
      transparency: 255,
      shininessWidth: 1,
    });
  }

  return { materials, idToIndex };
}

function readMaterial(materialNode, textureCanvases) {
  const children = materialNode.children || {};
  const diffuse = readColor255(children.Diffuse?.children?.Constant, { r: 255, g: 255, b: 255 });
  const emission = readColor255(children.Emission?.children?.Constant, { r: 0, g: 0, b: 0 });
  const specular = readColor255(children.Specular?.children?.Constant, { r: 20, g: 20, b: 20 });
  const shininessConstants = getFloatArray(children.Shininess?.children?.Constant);
  const transparencyConstants = getFloatArray(children.Transparency?.children?.Constant);
  const diffuseTextureName = getMapName(children.Diffuse);
  const emissiveTextureName = getMapName(children.Emission);
  const secondDiffuseTextureName = getMapName(children.Bump);
  const textureFlags = getInt32(children.Diffuse?.children?.Map?.children?.Flags) ?? 0;
  const emissiveTextureFlags = getInt32(children.Emission?.children?.Map?.children?.Flags) ?? 0;
  const secondDiffuseTextureFlags = getInt32(children.Bump?.children?.Map?.children?.Flags) ?? 0;
  const emissiveBlend = getMapBlend(children.Emission);
  const textureHasAlpha = textureHasAlphaChannel(textureCanvases, diffuseTextureName);
  const emissiveTextureHasAlpha = textureHasAlphaChannel(textureCanvases, emissiveTextureName);
  const secondDiffuseTextureHasAlpha = textureHasAlphaChannel(textureCanvases, secondDiffuseTextureName);

  return {
    name: materialNode.name,
    flags: 0x0004 | 0x0008 | (emission.r || emission.g || emission.b ? 0x0002 : 0),
    diffuse,
    emission,
    specular,
    transparency: Math.round((transparencyConstants?.[0] ?? 1) * 255),
    shininessWidth: shininessConstants?.[0] ?? 1,
    shininess: Math.round((shininessConstants?.[1] ?? shininessConstants?.[0] ?? 0.15) * 255),
    textureFlags: textureFlags | (textureHasAlpha ? 0x80000000 : 0),
    emissiveTextureFlags: emissiveTextureFlags | (emissiveTextureHasAlpha ? 0x80000000 : 0),
    secondDiffuseTextureFlags: secondDiffuseTextureFlags | (secondDiffuseTextureHasAlpha ? 0x80000000 : 0),
    diffuseTextureName,
    emissiveTextureName,
    emissiveBlend: Math.round((emissiveBlend ?? 1) * 255),
    secondDiffuseTextureName,
  };
}

function loadTextureCanvases(root) {
  const textureLibrary = root['Texture library']?.children || {};
  const textureCanvases = new Map();

  for (const [texName, texNode] of Object.entries(textureLibrary)) {
    if (!texNode.children) {
      continue;
    }

    const decoded = decodeTextureNode(texName, texNode);
    if (decoded) {
      textureCanvases.set(texName, decoded);
    }
  }

  return textureCanvases;
}

function decodeTextureNode(texName, texNode) {
  const mipNode = findMip0Node(texNode);
  const formatNode = findTextureFormatNode(texNode) || findTextureFormatNode(mipNode);

  if (!formatNode) {
    return null;
  }

  const width = getInt32(findChildCI(texNode, 'Image X size')) || getInt32(findChildCI(mipNode, 'Image X size'));
  const height = getInt32(findChildCI(texNode, 'Image Y size')) || getInt32(findChildCI(mipNode, 'Image Y size'));

  if (!width || !height) {
    return null;
  }

  const dataNode = findChildCI(formatNode, 'MIP0') || formatNode;
  const palette = findChildCI(formatNode, 'Palette RGB 888')?.value || findChildCI(dataNode, 'Palette RGB 888')?.value;
  const indices = findChildCI(dataNode, 'Image indices')?.value;
  const colors = findChildCI(dataNode, 'Image colors')?.value;
  const alpha = findChildCI(dataNode, 'Alpha 8 bit')?.value || findChildCI(dataNode, 'Image Alpha 8 bit')?.value;
  const formatName = formatNode.name;
  let rgba = null;

  if (isIndexedTextureFormat(formatName) && palette && indices) {
    rgba = palette8ToRGBA(indices, palette, width, height, alpha);
  } else if (formatName.toLowerCase() === 'true rgb 565' && colors) {
    rgba = rgb565WithAlphaToRGBA(colors, alpha, width, height);
  } else if (formatName.toLowerCase() === 'true 8 bit' && colors) {
    rgba = true8ToRGBA(colors, alpha, width, height);
  } else if (formatName.startsWith('Format_TRUE_') && colors) {
    rgba = formatTrueToRGBA(formatName, colors, alpha, width, height);
  } else if (formatName.startsWith('Format_PAL8') && palette && indices) {
    rgba = palette8ToRGBA(indices, palette, width, height, alpha);
  }

  if (!rgba) {
    return null;
  }

  return makeCanvas(texName, width, height, rgba, Boolean(alpha) || hasNonOpaqueAlpha(rgba));
}

function findMip0Node(node) {
  if (!node?.children) {
    return null;
  }

  const direct = findChildCI(node, 'MIP0');
  if (direct) {
    return direct;
  }

  for (const child of Object.values(node.children)) {
    const nested = findChildCI(child, 'MIP0');
    if (nested) {
      return nested;
    }
  }

  return null;
}

function findTextureFormatNode(node) {
  if (!node?.children) {
    return null;
  }

  return Object.values(node.children).find((child) => {
    const name = child.name || '';
    const lower = name.toLowerCase();
    return lower === 'palette 8 bit' ||
      lower === 'true rgb 565' ||
      lower === 'true 8 bit' ||
      name.startsWith('Format_');
  }) || null;
}

function mergeTextureCanvases(primary, secondary) {
  const result = new Map(primary);

  for (const [name, canvas] of secondary) {
    result.set(name, canvas);
  }

  return result;
}

function findPolyMeshEntries(rootNode) {
  const entries = [];

  function visit(node, path) {
    if (!node?.children) {
      return;
    }

    const meshNode = node.children['openFLAME 3D N-mesh'];
    if (meshNode?.children) {
      entries.push({
        name: path || node.name || 'Mesh',
        containerNode: node,
        meshNode,
      });
    }

    for (const child of Object.values(node.children)) {
      if (child?.children && child !== meshNode) {
        const childPath = child.name === 'openFLAME 3D N-mesh' ? path : [path, child.name].filter(Boolean).join('/');
        visit(child, childPath);
      }
    }
  }

  visit(rootNode, rootNode.name === '\\' ? '' : rootNode.name);
  return entries;
}

function createTextureResolver(textureCanvases) {
  const textureCache = new Map();

  return (material, slot) => {
    const textureName =
      slot === 'emissive'
        ? material.emissiveTextureName
        : slot === 'secondDiffuse'
          ? material.secondDiffuseTextureName
          : material.diffuseTextureName;

    if (!textureName) {
      return null;
    }

    const canvas = findTextureCanvas(textureCanvases, textureName);
    if (!canvas) {
      return null;
    }

    const cacheKey = `${slot}:${canvas.dataset.textureName || textureName}`;
    if (textureCache.has(cacheKey)) {
      return textureCache.get(cacheKey);
    }

    const texture = new THREE.CanvasTexture(canvas);
    texture.magFilter = THREE.NearestFilter;
    texture.minFilter = THREE.LinearMipmapLinearFilter;
    texture.generateMipmaps = true;
    textureCache.set(cacheKey, texture);
    return texture;
  };
}

function setActiveMesh(mesh, textureCanvases) {
  if (activeMesh) {
    scene.remove(activeMesh);
    disposeObject(activeMesh);
  }

  activeMesh = mesh;
  activeTextureCanvases = textureCanvases;
  scene.add(activeMesh);
}

function frameActiveMesh() {
  if (!activeMesh) {
    return;
  }

  activeMesh.updateMatrixWorld(true);
  const box = new THREE.Box3().setFromObject(activeMesh);
  const center = box.getCenter(cameraTarget);
  const size = box.getSize(new THREE.Vector3());
  const maxSize = Math.max(size.x, size.y, size.z, 1);

  grid.scale.setScalar(Math.max(1, maxSize / 10));
  grid.position.copy(center);
  grid.position.y = box.min.y;
  axes.position.copy(center);

  orbit.distance = maxSize * 1.7;
  orbit.yaw = Math.PI / 4;
  orbit.pitch = Math.PI / 6;
  camera.near = Math.max(0.01, orbit.distance / 1000);
  camera.far = Math.max(1000, orbit.distance * 100);
  camera.updateProjectionMatrix();
  updateCamera();
}

function installPointerControls(target) {
  let dragging = false;
  let panning = false;
  let previousX = 0;
  let previousY = 0;

  target.addEventListener('pointerdown', (event) => {
    dragging = true;
    panning = event.button === 1 || event.button === 2 || event.shiftKey;
    previousX = event.clientX;
    previousY = event.clientY;
    target.setPointerCapture(event.pointerId);
  });

  target.addEventListener('pointermove', (event) => {
    if (!dragging) {
      return;
    }

    const dx = event.clientX - previousX;
    const dy = event.clientY - previousY;
    previousX = event.clientX;
    previousY = event.clientY;

    if (panning) {
      panCamera(dx, dy);
    } else {
      orbit.yaw -= dx * 0.008;
      orbit.pitch = clamp(orbit.pitch - dy * 0.008, -Math.PI / 2 + 0.05, Math.PI / 2 - 0.05);
      updateCamera();
    }
  });

  target.addEventListener('pointerup', (event) => {
    dragging = false;
    target.releasePointerCapture(event.pointerId);
  });

  target.addEventListener('contextmenu', (event) => event.preventDefault());
  target.addEventListener('wheel', (event) => {
    event.preventDefault();
    orbit.distance = Math.max(0.01, orbit.distance * Math.exp(event.deltaY * 0.001));
    updateCamera();
  }, { passive: false });
}

function panCamera(dx, dy) {
  const offset = new THREE.Vector3().subVectors(camera.position, cameraTarget);
  const right = new THREE.Vector3().crossVectors(camera.up, offset).normalize();
  const up = new THREE.Vector3().crossVectors(offset, right).normalize();
  const scale = orbit.distance / Math.max(canvas.clientWidth, canvas.clientHeight, 1);

  cameraTarget.addScaledVector(right, dx * scale);
  cameraTarget.addScaledVector(up, dy * scale);
  updateCamera();
}

function updateCamera() {
  const cosPitch = Math.cos(orbit.pitch);
  camera.position.set(
    cameraTarget.x + orbit.distance * Math.sin(orbit.yaw) * cosPitch,
    cameraTarget.y + orbit.distance * Math.sin(orbit.pitch),
    cameraTarget.z + orbit.distance * Math.cos(orbit.yaw) * cosPitch,
  );
  camera.lookAt(cameraTarget);
}

function resizeRenderer() {
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;

  renderer.setSize(width, height, false);
  camera.aspect = width / Math.max(height, 1);
  camera.updateProjectionMatrix();
}

function animate() {
  animationFrame = requestAnimationFrame(animate);
  renderer.render(scene, camera);
}

function setWireframe(enabled) {
  if (!activeMesh) {
    return;
  }

  activeMesh.traverse((object) => {
    if (object.isMesh) {
      object.material.wireframe = enabled;
    }
  });
}

function updateStats(stats, textureCanvases) {
  statsLine.textContent = `${stats.vertices} vertices | ${stats.faces} faces | ${stats.materials} materials | ${textureCanvases.size} textures`;
}

function setStatus(message, isError = false) {
  statusLine.textContent = message;
  statusLine.classList.toggle('error', isError);
}

function findUtfRootNode(root) {
  if (root['\\']) {
    return root['\\'];
  }

  return Object.values(root).find((node) => node?.children) || null;
}

function getFloatArray(node) {
  if (!node?.value) {
    return null;
  }

  return new Float32Array(node.value.buffer, node.value.byteOffset, Math.floor(node.value.byteLength / 4));
}

function getIntArray(node) {
  if (!node?.value) {
    return null;
  }

  return new Int32Array(node.value.buffer, node.value.byteOffset, Math.floor(node.value.byteLength / 4));
}

function getInt32(node) {
  if (!node?.value || node.value.byteLength < 4) {
    return null;
  }

  return new DataView(node.value.buffer, node.value.byteOffset, node.value.byteLength).getInt32(0, true);
}

function getString(node) {
  if (!node?.value) {
    return null;
  }

  let end = 0;
  while (end < node.value.length && node.value[end] !== 0) {
    end += 1;
  }

  return new TextDecoder('ascii').decode(node.value.slice(0, end));
}

function readColor255(node, fallback) {
  if (!node?.value) {
    return fallback;
  }

  if (node.value.byteLength >= 12) {
    const view = new DataView(node.value.buffer, node.value.byteOffset, node.value.byteLength);
    return {
      r: clamp(Math.round(view.getFloat32(0, true) * 255), 0, 255),
      g: clamp(Math.round(view.getFloat32(4, true) * 255), 0, 255),
      b: clamp(Math.round(view.getFloat32(8, true) * 255), 0, 255),
    };
  }

  if (node.value.byteLength >= 3) {
    return {
      r: node.value[0],
      g: node.value[1],
      b: node.value[2],
    };
  }

  return fallback;
}

function getMapName(propertyNode) {
  return getString(propertyNode?.children?.Map?.children?.Name);
}

function getMapBlend(propertyNode) {
  const constants = getFloatArray(propertyNode?.children?.Map?.children?.Blend);
  return constants?.[0] ?? null;
}

function textureHasAlphaChannel(textureCanvases, textureName) {
  return textureName ? findTextureCanvas(textureCanvases, textureName)?.dataset.hasAlpha === 'true' : false;
}

function vector3Array(values) {
  if (!values) {
    return [];
  }

  const out = [];
  for (let i = 0; i + 2 < values.length; i += 3) {
    out.push({ x: values[i], y: values[i + 1], z: values[i + 2] });
  }
  return out;
}

function uvArray(values) {
  if (!values) {
    return [];
  }

  const out = [];
  for (let i = 0; i + 1 < values.length; i += 2) {
    out.push({ u: values[i], v: values[i + 1] });
  }
  return out;
}

function makeCanvas(name, width, height, rgba, hasAlpha) {
  const textureCanvas = document.createElement('canvas');
  textureCanvas.width = width;
  textureCanvas.height = height;
  textureCanvas.dataset.textureName = name;
  textureCanvas.dataset.hasAlpha = hasAlpha ? 'true' : 'false';
  textureCanvas.getContext('2d').putImageData(new ImageData(rgba, width, height), 0, 0);
  return textureCanvas;
}

function palette8ToRGBA(indices, palette, width, height, alpha = null) {
  const pixelCount = Math.min(indices.byteLength, width * height);
  const out = new Uint8ClampedArray(width * height * 4);

  for (let i = 0; i < pixelCount; i += 1) {
    const paletteIndex = indices[i] * 3;
    out[i * 4] = palette[paletteIndex] || 0;
    out[i * 4 + 1] = palette[paletteIndex + 1] || 0;
    out[i * 4 + 2] = palette[paletteIndex + 2] || 0;
    out[i * 4 + 3] = alpha && i < alpha.byteLength ? alpha[i] : 255;
  }

  return out;
}

function rgb565WithAlphaToRGBA(colors, alpha, width, height) {
  const view = new DataView(colors.buffer, colors.byteOffset, colors.byteLength);
  const pixelCount = Math.min(Math.floor(view.byteLength / 2), width * height);
  const out = new Uint8ClampedArray(width * height * 4);

  for (let i = 0; i < pixelCount; i += 1) {
    const value = view.getUint16(i * 2, true);
    out[i * 4] = Math.round(((value >> 11) & 0x1f) * 255 / 31);
    out[i * 4 + 1] = Math.round(((value >> 5) & 0x3f) * 255 / 63);
    out[i * 4 + 2] = Math.round((value & 0x1f) * 255 / 31);
    out[i * 4 + 3] = alpha && i < alpha.byteLength ? alpha[i] : 255;
  }

  return out;
}

function true8ToRGBA(colors, alpha, width, height) {
  const pixelCount = Math.min(colors.byteLength, width * height);
  const out = new Uint8ClampedArray(width * height * 4);

  for (let i = 0; i < pixelCount; i += 1) {
    const value = colors[i];
    out[i * 4] = value;
    out[i * 4 + 1] = value;
    out[i * 4 + 2] = value;
    out[i * 4 + 3] = alpha && i < alpha.byteLength ? alpha[i] : 255;
  }

  return out;
}

function formatTrueToRGBA(formatName, colors, alpha, width, height) {
  const bits = parseFormatTrueBits(formatName);

  if (!bits) {
    return null;
  }

  const [rBits, gBits, bBits, aBits] = bits;
  const bitsPerPixel = rBits + gBits + bBits + aBits;
  const bytesPerPixel = Math.ceil(bitsPerPixel / 8);
  const pixelCount = Math.min(Math.floor(colors.byteLength / bytesPerPixel), width * height);
  const out = new Uint8ClampedArray(width * height * 4);

  for (let i = 0; i < pixelCount; i += 1) {
    let value = 0;
    const offset = i * bytesPerPixel;

    for (let byte = 0; byte < bytesPerPixel; byte += 1) {
      value |= (colors[offset + byte] || 0) << (byte * 8);
    }

    const bMask = (1 << bBits) - 1;
    const gMask = (1 << gBits) - 1;
    const rMask = (1 << rBits) - 1;
    const aMask = aBits ? (1 << aBits) - 1 : 0;
    const b = bBits ? value & bMask : 0;
    const g = gBits ? (value >> bBits) & gMask : 0;
    const r = rBits ? (value >> (bBits + gBits)) & rMask : 0;
    const embeddedAlpha = aBits ? (value >> (bBits + gBits + rBits)) & aMask : null;

    out[i * 4] = expandBits(r, rBits);
    out[i * 4 + 1] = expandBits(g, gBits);
    out[i * 4 + 2] = expandBits(b, bBits);
    out[i * 4 + 3] = alpha && i < alpha.byteLength
      ? alpha[i]
      : embeddedAlpha == null
        ? 255
        : expandBits(embeddedAlpha, aBits);
  }

  return out;
}

function parseFormatTrueBits(formatName) {
  const parts = formatName.replace(/^Format_TRUE_/, '').split('_').filter(Boolean).map((part) => Number(part));
  const componentCount = parts[0];
  const sizes = parts.slice(1, 1 + componentCount);

  if (!componentCount || sizes.some((size) => !Number.isFinite(size))) {
    return null;
  }

  if (componentCount === 2) {
    return [sizes[0] || 0, 0, 0, sizes[1] || 0];
  }

  return [
    sizes[0] || 0,
    sizes[1] || 0,
    sizes[2] || 0,
    sizes[3] || 0,
  ];
}

function isIndexedTextureFormat(formatName) {
  const lower = formatName.toLowerCase();
  return lower === 'palette 8 bit' || formatName.startsWith('Format_PAL8');
}

function expandBits(value, bits) {
  if (!bits) {
    return 0;
  }

  return Math.round((value * 255) / ((1 << bits) - 1));
}

function hasNonOpaqueAlpha(rgba) {
  for (let i = 3; i < rgba.length; i += 4) {
    if (rgba[i] !== 255) {
      return true;
    }
  }

  return false;
}

function findTextureCanvas(textureCanvases, textureName) {
  if (textureCanvases.has(textureName)) {
    return textureCanvases.get(textureName);
  }

  const baseName = textureName.split('.')[0].toLowerCase();
  const match = Array.from(textureCanvases.keys()).find((key) => key.split('.')[0].toLowerCase() === baseName);
  return match ? textureCanvases.get(match) : null;
}

function findChildCI(node, name) {
  if (!node?.children) {
    return null;
  }

  const lower = name.toLowerCase();
  return Object.values(node.children).find((child) => child.name.toLowerCase() === lower) || null;
}

function disposeObject(root) {
  root.traverse((object) => {
    if (object.geometry) {
      object.geometry.dispose();
    }

    const materials = Array.isArray(object.material) ? object.material : [object.material];
    materials.filter(Boolean).forEach((material) => {
      for (const value of Object.values(material)) {
        if (value?.isTexture) {
          value.dispose();
        }
      }
      material.dispose();
    });
  });
}

function naturalSort(a, b) {
  return a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' });
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}
