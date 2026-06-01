import * as THREE from 'three';
import { loadUtfFile, parseUtfXmlString } from './utfParser.js';
import { createThreePolyMesh } from './ThreePolyMesh.js';

const PSP_NUM_COLOR_KEYS = 32;
const PSP_TEXTURE_NAME_LEN = 16;
const PSP_F_RELATIVE_TRANSFORM = 1 << 0;
const PSP_F_RELATIVE_VELOCITY = 1 << 1;
const PSP_F_RENDER_DITHER = 1 << 4;
const PSP_F_RENDER_PARTICLE_LIFE = 1 << 3;
const PSP_F_RENDER_FOG = 1 << 5;
const D3DBLEND_ZERO = 1;
const D3DBLEND_ONE = 2;
const D3DBLEND_SRCALPHA = 5;
const D3DBLEND_INVSRCALPHA = 6;
const CHANNEL_DT_FLOAT = 1;
const CHANNEL_DT_VECTOR = 2;
const CHANNEL_DT_QUATERNION = 4;

const fileInput = document.querySelector('#utf-file');
const openButton = document.querySelector('#open-file');
const resetButton = document.querySelector('#reset-view');
const animStartButton = document.querySelector('#anim-start');
const animStopButton = document.querySelector('#anim-stop');
const animRestartButton = document.querySelector('#anim-restart');
const animResetButton = document.querySelector('#anim-reset');
const animLoopToggle = document.querySelector('#anim-loop');
const animPingPongToggle = document.querySelector('#anim-pingpong');
const wireframeToggle = document.querySelector('#wireframe');
const statusLine = document.querySelector('#status');
const statsLine = document.querySelector('#stats');
const animationPanel = document.querySelector('#animation-panel');
const animationTitle = document.querySelector('#animation-title');
const animationSelect = document.querySelector('#animation-select');
const animationMeta = document.querySelector('#animation-meta');
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
let activeParticleSystems = [];
let activeAnimationController = null;
let animationFrame = 0;
let lastTime = performance.now();

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
animStartButton.addEventListener('click', () => {
  if (activeAnimationController) {
    activeAnimationController.play();
    updateAnimationButtons();
    updateAnimationPanel();
  }
});
animStopButton.addEventListener('click', () => {
  if (activeAnimationController) {
    activeAnimationController.stop();
    updateAnimationButtons();
    updateAnimationPanel();
  }
});
animRestartButton.addEventListener('click', () => {
  if (activeAnimationController) {
    activeAnimationController.restart();
    updateAnimationButtons();
    updateAnimationPanel();
  }
});
animResetButton.addEventListener('click', () => {
  if (activeAnimationController) {
    activeAnimationController.reset();
    updateAnimationButtons();
    updateAnimationPanel();
  }
});
animLoopToggle.addEventListener('change', () => {
  if (activeAnimationController) {
    activeAnimationController.loop = animLoopToggle.checked;
  }
});
animPingPongToggle.addEventListener('change', () => {
  if (activeAnimationController) {
    activeAnimationController.pingPong = animPingPongToggle.checked;
  }
});
animationSelect.addEventListener('change', () => {
  if (activeAnimationController) {
    activeAnimationController.selectClip(Number(animationSelect.value));
    updateAnimationButtons();
    updateAnimationPanel();
  }
});
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
      const compound = await buildCompoundObject(rootNode, meshEntries, textureCanvases);
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

async function buildCompoundObject(rootNode, meshEntries, textureCanvases) {
  const object = new THREE.Group();
  const partInfos = readCompoundParts(rootNode.children?.Cmpnd);
  const joints = readCompoundJoints(rootNode.children?.Cmpnd?.children?.Cons);
  const byFileName = new Map(meshEntries.map((entry) => [entry.containerNode.name.toLowerCase(), entry]));
  const byPartName = new Map();
  const stats = { vertices: 0, faces: 0, materials: 0, particles: 0, animations: 0, hardpoints: 0 };
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
      const hardpoints = readHardpoints(entry.containerNode);
      addHardpointMarkers(meshGroup, hardpoints, entry.name);
      object.add(meshGroup);
      mergedTextures = mergeTextureCanvases(mergedTextures, partTextures);
      stats.vertices += meshData.objectVertexList.length;
      stats.faces += meshData.faceGroups.reduce((total, group) => total + group.faceCnt, 0);
      stats.materials += meshData.materialList.length;
      stats.hardpoints += hardpoints.length;
    }

    const particleEntries = findEmbeddedParticleEntries(rootNode);
    for (const entry of particleEntries) {
      const partTextures = mergeTextureCanvases(mergedTextures, loadTextureCanvases(entry.textureChildren));
      const particleObject = createParticleObject(entry.parameters, partTextures, entry.name);
      object.add(particleObject);
      mergedTextures = mergeTextureCanvases(mergedTextures, partTextures);
      stats.particles += 1;
    }

    return { object, textureCanvases: mergedTextures, stats };
  }

  for (const part of partInfos) {
    const entry = byFileName.get(part.fileName.toLowerCase());
    if (entry) {
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
      const hardpoints = readHardpoints(entry.containerNode);
      addHardpointMarkers(meshGroup, hardpoints, part.objectName);
      byPartName.set(part.objectName, meshGroup);
      mergedTextures = mergeTextureCanvases(mergedTextures, partTextures);
      stats.vertices += meshData.objectVertexList.length;
      stats.faces += meshData.faceGroups.reduce((total, group) => total + group.faceCnt, 0);
      stats.materials += meshData.materialList.length;
      stats.hardpoints += hardpoints.length;
      continue;
    }

    const particleEntry = await loadCompoundParticlePart(rootNode, part);
    if (particleEntry) {
      const partTextures = mergeTextureCanvases(mergedTextures, loadTextureCanvases(particleEntry.textureChildren));
      const particleObject = createParticleObject(particleEntry.parameters, partTextures, part.objectName);
      particleObject.userData.fileName = part.fileName;
      particleObject.userData.partName = part.objectName;
      byPartName.set(part.objectName, particleObject);
      mergedTextures = mergeTextureCanvases(mergedTextures, partTextures);
      stats.particles += 1;
    }
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
  const animationController = createCompoundAnimationController(rootNode, byPartName, joints);
  if (animationController) {
    object.userData.animationController = animationController;
    stats.animations = animationController.clipCount;
  }

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

function readHardpoints(containerNode) {
  const hardpointRoot = containerNode?.children?.Hardpoints;
  if (!hardpointRoot?.children) {
    return [];
  }

  const out = [];

  for (const groupNode of Object.values(hardpointRoot.children)) {
    if (!groupNode?.children) {
      continue;
    }

    for (const hardpointNode of Object.values(groupNode.children)) {
      const positionBytes = hardpointNode?.children?.Position?.value;
      if (!positionBytes || positionBytes.byteLength < 12) {
        continue;
      }

      const view = new DataView(positionBytes.buffer, positionBytes.byteOffset, positionBytes.byteLength);
      out.push({
        name: hardpointNode.name || `hardpoint_${out.length + 1}`,
        type: groupNode.name || 'Hardpoint',
        position: readVector3(view, 0),
      });
    }
  }

  return out;
}

function addHardpointMarkers(target, hardpoints, partName) {
  if (hardpoints.length === 0) {
    return;
  }

  target.updateMatrixWorld(true);
  const box = new THREE.Box3().setFromObject(target);
  const size = box.getSize(new THREE.Vector3());
  const maxSize = Math.max(size.x, size.y, size.z, 1);
  const radius = clamp(maxSize * 0.018, 0.5, 25);
  const markerRoot = new THREE.Group();
  markerRoot.name = `${partName || target.name || 'Part'} hardpoints`;
  markerRoot.userData.hardpointOverlay = true;

  for (const hardpoint of hardpoints) {
    const marker = createHardpointMarker(hardpoint, radius);
    markerRoot.add(marker);
  }

  target.add(markerRoot);
}

function createHardpointMarker(hardpoint, radius) {
  const marker = new THREE.Group();
  marker.name = hardpoint.name;
  marker.position.copy(hardpoint.position);
  marker.userData.hardpoint = hardpoint;

  const sphere = new THREE.Mesh(
    new THREE.SphereGeometry(radius, 16, 12),
    new THREE.MeshBasicMaterial({
      color: 0xffd24a,
      depthTest: false,
      depthWrite: false,
      transparent: true,
      opacity: 0.95,
    }),
  );
  sphere.userData.hardpointMarker = true;
  sphere.renderOrder = 1000;
  marker.add(sphere);

  const labelMaterial = makeHardpointLabelMaterial(hardpoint.name);
  const labelAspect = labelMaterial.userData.aspect || 3;
  const label = new THREE.Sprite(labelMaterial);
  label.position.set(radius * 1.8, radius * 1.8, 0);
  label.scale.set(radius * 3 * labelAspect, radius * 3, 1);
  label.renderOrder = 1001;
  marker.add(label);

  return marker;
}

function makeHardpointLabelMaterial(text) {
  const canvasTexture = document.createElement('canvas');
  const ctx = canvasTexture.getContext('2d');
  const fontSize = 28;
  ctx.font = `600 ${fontSize}px Inter, Segoe UI, sans-serif`;
  const width = Math.ceil(Math.max(96, ctx.measureText(text).width + 26));
  const height = 44;
  canvasTexture.width = width;
  canvasTexture.height = height;
  ctx.font = `600 ${fontSize}px Inter, Segoe UI, sans-serif`;
  ctx.textBaseline = 'middle';
  ctx.fillStyle = 'rgba(11, 13, 18, 0.82)';
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = 'rgba(255, 210, 74, 0.85)';
  ctx.lineWidth = 2;
  ctx.strokeRect(1, 1, width - 2, height - 2);
  ctx.fillStyle = '#fff2b0';
  ctx.fillText(text, 13, height / 2);

  const texture = new THREE.CanvasTexture(canvasTexture);
  texture.colorSpace = THREE.SRGBColorSpace;
  const material = new THREE.SpriteMaterial({
    map: texture,
    transparent: true,
    depthTest: false,
    depthWrite: false,
  });
  material.userData.aspect = width / height;
  return material;
}

async function loadCompoundParticlePart(rootNode, part) {
  if (!part.fileName.toLowerCase().endsWith('.pte')) {
    return null;
  }

  const embedded = findEmbeddedParticleEntry(rootNode, part.fileName);
  if (embedded) {
    return embedded;
  }

  const url = `../xml_dump/${encodeURIComponent(part.fileName)}.xml`;
  try {
    const response = await fetch(url);
    if (!response.ok) {
      console.warn(`Particle part ${part.fileName} was not embedded and ${url} returned ${response.status}.`);
      return null;
    }

    const utfRoot = parseUtfXmlString(await response.text());
    const particleRoot = findUtfRootNode(utfRoot);
    return particleRoot?.children ? loadParticleParameters(particleRoot.children) : null;
  } catch (error) {
    console.warn(`Could not load particle part ${part.fileName}.`, error);
    return null;
  }
}

function findEmbeddedParticleEntry(rootNode, fileName) {
  const lowerFileName = fileName.toLowerCase();
  return findEmbeddedParticleEntries(rootNode).find((entry) =>
    entry.containerNode.name.toLowerCase() === lowerFileName ||
    entry.name.toLowerCase() === lowerFileName ||
    `${entry.name.toLowerCase()}.xml` === `${lowerFileName}.xml`,
  ) || null;
}

function findEmbeddedParticleEntries(rootNode) {
  const entries = [];

  function visit(node, path) {
    if (!node?.children) {
      return;
    }

    try {
      const particle = loadParticleParameters(node.children);
      entries.push({
        name: path || node.name || 'Particle',
        containerNode: node,
        parameters: particle.parameters,
        textureChildren: particle.textureChildren,
      });
      return;
    } catch {
      // Not a particle node; keep walking.
    }

    for (const child of Object.values(node.children)) {
      if (child?.children) {
        visit(child, [path, child.name].filter(Boolean).join('/'));
      }
    }
  }

  visit(rootNode, rootNode.name === '\\' ? '' : rootNode.name);
  return entries;
}

function loadParticleParameters(children) {
  if (children.ParticleSystemParameters?.value) {
    return {
      parameters: parseParticleSystemParameters(children.ParticleSystemParameters.value),
      textureChildren: children,
    };
  }

  const eventChildren = children['Particle Event']?.children;
  const eventBytes = eventChildren?.['particle1.Def']?.value;
  if (eventBytes) {
    return {
      parameters: parseLegacyEventDef(eventBytes),
      textureChildren: eventChildren,
    };
  }

  throw new Error('No particle parameters found.');
}

function parseParticleSystemParameters(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let offset = 0;
  const parameters = createDefaultParticleParameters();

  parameters.pspFlags = view.getUint32(offset, true);
  offset += 4;
  parameters.colorFrames = [];
  for (let i = 0; i < PSP_NUM_COLOR_KEYS; i += 1) {
    parameters.colorFrames.push({
      r: view.getFloat32(offset, true),
      g: view.getFloat32(offset + 4, true),
      b: view.getFloat32(offset + 8, true),
      a: view.getFloat32(offset + 12, true),
    });
    offset += 16;
  }

  parameters.colorKeyFrameBits = view.getUint32(offset, true);
  offset += 4;
  parameters.textureName = readCString(bytes, offset, PSP_TEXTURE_NAME_LEN);
  offset += PSP_TEXTURE_NAME_LEN;
  parameters.textureFps = view.getFloat32(offset, true);
  offset += 4;
  parameters.srcBlend = view.getUint32(offset, true);
  offset += 4;
  parameters.dstBlend = view.getUint32(offset, true);
  offset += 4;
  parameters.gravity = readVector3(view, offset);
  offset += 12;
  parameters.emitterDirection = readVector3(view, offset);
  offset += 12;
  parameters.emitterNozzleSize = view.getFloat32(offset, true);
  offset += 4;
  parameters.emitterNozzleDamp = readVector3(view, offset);
  offset += 12;
  parameters.initialParticleCount = view.getInt32(offset, true);
  offset += 4;
  parameters.maxParticleCount = view.getInt32(offset, true);
  offset += 4;
  parameters.lifetime = view.getFloat32(offset, true);
  offset += 4;
  parameters.frequency = view.getFloat32(offset, true);
  offset += 4;
  parameters.particleLifetime = view.getFloat32(offset, true);
  offset += 4;
  parameters.particlePositionRandomizer = view.getFloat32(offset, true);
  offset += 4;
  parameters.particleVelocity = view.getFloat32(offset, true);
  offset += 4;
  parameters.particleVelocityRandomizer = view.getFloat32(offset, true);
  offset += 4;
  parameters.particleTwistVelocity = view.getFloat32(offset, true);
  offset += 4;
  parameters.particleSize = view.getFloat32(offset, true);
  offset += 4;
  parameters.particleSizeVelocity = view.getFloat32(offset, true);
  offset += 4;
  parameters.boundingSphereRadius = view.getFloat32(offset, true);
  return parameters;
}

function parseLegacyEventDef(bytes) {
  if (bytes.byteLength < 176) {
    throw new Error(`Legacy particle EventDef is too small: ${bytes.byteLength} bytes.`);
  }

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const parameters = createDefaultParticleParameters();
  const alpha = view.getFloat32(0, true);
  const alphaDecay = view.getFloat32(4, true);
  const color = readVector3(view, 16);
  const colorVelocity = readVector3(view, 40);
  const frequency = view.getFloat32(52, true);
  const gravity = view.getFloat32(56, true);
  const lifetime = view.getInt32(60, true);
  const maxParticles = view.getInt32(64, true);
  const nozzle = view.getFloat32(72, true);
  const nozzleDamp = readVector3(view, 76);
  const nParticles = view.getFloat32(88, true);
  const partLife = view.getUint32(92, true);
  const randPosition = view.getFloat32(100, true);
  const size = view.getFloat32(104, true);
  const sizeVelocity = view.getFloat32(108, true);
  const textureName = readCString(bytes, 116, PSP_TEXTURE_NAME_LEN);
  const twistSpeed = view.getFloat32(132, true);
  const velocity = view.getFloat32(140, true);
  const velocityRand = view.getFloat32(144, true);
  const dither = view.getUint8(148);
  const radius = view.getFloat32(152, true);
  let direction = readVector3(view, 156);

  if (direction.lengthSq() <= 0.000001) {
    direction = new THREE.Vector3(1, 1, 1);
  }

  parameters.lifetime = lifetime * 0.001;
  parameters.frequency = frequency;
  parameters.initialParticleCount = nParticles;
  parameters.maxParticleCount = maxParticles;
  parameters.emitterDirection = direction;
  parameters.emitterNozzleSize = nozzle;
  parameters.emitterNozzleDamp = nozzleDamp;
  parameters.gravity = new THREE.Vector3(0, 0, gravity * 1000);
  parameters.particleLifetime = partLife * 0.001;
  parameters.particlePositionRandomizer = randPosition;
  parameters.particleSize = size;
  parameters.particleSizeVelocity = sizeVelocity * 1000;
  parameters.particleTwistVelocity = twistSpeed * 1000;
  parameters.particleVelocity = velocity * 1000;
  parameters.particleVelocityRandomizer = velocityRand;
  parameters.textureName = textureName;
  parameters.textureFps = view.getFloat32(172, true);
  parameters.boundingSphereRadius = radius;

  if (dither) {
    parameters.pspFlags |= PSP_F_RENDER_DITHER;
  }

  const legacyFlags = view.getUint32(168, true);
  if (legacyFlags & 1) {
    parameters.pspFlags |= PSP_F_RELATIVE_VELOCITY;
  }
  if (legacyFlags & 2) {
    parameters.pspFlags |= PSP_F_RELATIVE_TRANSFORM;
  }

  if (bytes.byteLength >= 204) {
    const srcBlend = view.getUint32(176, true);
    const dstBlend = view.getUint32(180, true);
    const gravityVec = readVector3(view, 184);
    if (srcBlend > 0) {
      parameters.srcBlend = srcBlend;
    }
    if (dstBlend > 0) {
      parameters.dstBlend = dstBlend;
    }
    parameters.gravity = new THREE.Vector3(gravityVec.x * 1000, gravityVec.y * 1000, (gravityVec.z + gravity) * 1000);
    if (view.getInt32(196, true)) {
      parameters.pspFlags |= PSP_F_RENDER_PARTICLE_LIFE;
    }
    if (view.getInt32(200, true)) {
      parameters.pspFlags |= PSP_F_RENDER_FOG;
    }
  }

  if (bytes.byteLength >= 720 && view.getUint32(204, true)) {
    parameters.colorKeyFrameBits = view.getUint32(204, true) | 0x80000001;
    parameters.colorFrames = [];
    for (let i = 0; i < PSP_NUM_COLOR_KEYS; i += 1) {
      parameters.colorFrames.push({
        r: view.getFloat32(208 + i * 16, true),
        g: view.getFloat32(212 + i * 16, true),
        b: view.getFloat32(216 + i * 16, true),
        a: view.getFloat32(220 + i * 16, true),
      });
    }
  } else if (colorVelocity.lengthSq() > 0) {
    const durationMs = partLife || lifetime || 1000;
    const frameStepMs = durationMs / PSP_NUM_COLOR_KEYS;
    let r = color.x;
    let g = color.y;
    let b = color.z;
    let a = alpha;
    parameters.colorFrames = [];
    for (let i = 0; i < PSP_NUM_COLOR_KEYS; i += 1) {
      parameters.colorFrames.push({ r: clamp(r, 0, 1), g: clamp(g, 0, 1), b: clamp(b, 0, 1), a: clamp(a, 0, 1) });
      r += frameStepMs * 0.001 * colorVelocity.x;
      g += frameStepMs * 0.001 * colorVelocity.y;
      b += frameStepMs * 0.001 * colorVelocity.z;
      a += frameStepMs * 0.001 * alphaDecay;
    }
  } else {
    parameters.colorFrames = parameters.colorFrames.map(() => ({ r: color.x, g: color.y, b: color.z, a: alpha }));
  }

  return parameters;
}

function createDefaultParticleParameters() {
  return {
    pspFlags: 0,
    colorFrames: Array.from({ length: PSP_NUM_COLOR_KEYS }, () => ({ r: 1, g: 1, b: 1, a: 1 })),
    colorKeyFrameBits: 0x80000001,
    textureName: '',
    textureFps: 0,
    srcBlend: D3DBLEND_ONE,
    dstBlend: D3DBLEND_ONE,
    gravity: new THREE.Vector3(),
    emitterDirection: new THREE.Vector3(1, 1, 1),
    emitterNozzleSize: 0,
    emitterNozzleDamp: new THREE.Vector3(),
    initialParticleCount: 0,
    maxParticleCount: 0,
    lifetime: 0,
    frequency: 0,
    particleLifetime: 0,
    particlePositionRandomizer: 0,
    particleVelocity: 0,
    particleVelocityRandomizer: 0,
    particleTwistVelocity: 0,
    particleSize: 0,
    particleSizeVelocity: 0,
    boundingSphereRadius: 0,
  };
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

function createCompoundAnimationController(rootNode, byPartName, joints) {
  const clips = readJointAnimationClips(rootNode.children?.Animation, joints);
  if (clips.length === 0) {
    return null;
  }

  return new CompoundAnimationController(clips, byPartName);
}

function readJointAnimationClips(animationNode, joints) {
  const scriptRoot = animationNode?.children?.Script;
  if (!scriptRoot?.children) {
    return [];
  }

  const namedChannels = readNamedAnimationChannels(animationNode.children?.Chnl);
  const jointByPair = new Map(joints.map((joint) => [jointKey(joint.parent, joint.child), joint]));
  const clips = [];

  for (const scriptNode of Object.values(scriptRoot.children)) {
    if (!scriptNode?.children) {
      continue;
    }

    const tracks = [];
    let duration = 0;

    for (const mapNode of Object.values(scriptNode.children)) {
      if (!mapNode?.children) {
        continue;
      }

      const isJointMap = mapNode.name?.startsWith('Joint map');
      const isObjectMap = mapNode.name?.startsWith('Object map');
      if (!isJointMap && !isObjectMap) {
        continue;
      }

      const parent = getString(mapNode.children['Parent name']);
      const child = getString(mapNode.children['Child name']);
      const targetName = isJointMap ? child : parent;
      if (!parent || !targetName) {
        continue;
      }

      const channelName = getString(mapNode.children['Channel name']);
      const channel = channelName
        ? namedChannels.get(channelName)
        : readAnimationChannel(mapNode.children.Channel);
      if (!channel) {
        continue;
      }

      if (isJointMap) {
        const joint = jointByPair.get(jointKey(parent, child));
        if (!joint) {
          continue;
        }

        tracks.push({ targetType: 'joint', parent, child, targetName, joint, channel });
      } else {
        tracks.push({ targetType: 'object', parent, child: '', targetName, channel });
      }

      duration = Math.max(duration, channel.duration);
    }

    if (tracks.length > 0 && duration > 0) {
      clips.push({
        name: scriptNode.name || `Script ${clips.length + 1}`,
        duration,
        tracks,
      });
    }
  }

  return clips;
}

function readNamedAnimationChannels(channelRoot) {
  const channels = new Map();
  if (!channelRoot?.children) {
    return channels;
  }

  for (const channelNode of Object.values(channelRoot.children)) {
    const channel = readAnimationChannel(channelNode);
    if (channel) {
      channels.set(channelNode.name, channel);
    }
  }

  return channels;
}

function readAnimationChannel(channelNode) {
  const header = readAnimationChannelHeader(channelNode);
  if (!header) {
    return null;
  }

  if ((header.type & (CHANNEL_DT_VECTOR | CHANNEL_DT_QUATERNION)) === (CHANNEL_DT_VECTOR | CHANNEL_DT_QUATERNION)) {
    return readFullTransformChannel(channelNode, header);
  }

  if ((header.type & CHANNEL_DT_QUATERNION) !== 0) {
    return readQuaternionChannel(channelNode, header);
  }

  if ((header.type & CHANNEL_DT_VECTOR) !== 0) {
    return readVectorChannel(channelNode, header);
  }

  if ((header.type & CHANNEL_DT_FLOAT) !== 0) {
    return readFloatChannel(channelNode, header);
  }

  return null;
}

function readAnimationChannelHeader(channelNode) {
  const headerBytes = channelNode?.children?.Header?.value;
  const frameBytes = channelNode?.children?.Frames?.value;
  if (!headerBytes || !frameBytes || headerBytes.byteLength < 12) {
    return null;
  }

  const headerView = new DataView(headerBytes.buffer, headerBytes.byteOffset, headerBytes.byteLength);
  return {
    frameBytes,
    frameCount: headerView.getUint32(0, true),
    captureRate: headerView.getFloat32(4, true),
    type: headerView.getUint32(8, true),
  };
}

function readFullTransformChannel(channelNode, header = readAnimationChannelHeader(channelNode)) {
  if (!header) {
    return null;
  }

  const { frameBytes, frameCount, captureRate, type } = header;
  const hasVector = (type & CHANNEL_DT_VECTOR) !== 0;
  const hasQuaternion = (type & CHANNEL_DT_QUATERNION) !== 0;

  if (!hasVector || !hasQuaternion || frameCount < 1) {
    return null;
  }

  const frameSize = (captureRate < 0 ? 4 : 0) + 12 + 16;
  if (frameBytes.byteLength < frameSize * frameCount) {
    return null;
  }

  const frameView = new DataView(frameBytes.buffer, frameBytes.byteOffset, frameBytes.byteLength);
  const frames = [];

  for (let i = 0; i < frameCount; i += 1) {
    const offset = i * frameSize;
    const dataOffset = captureRate < 0 ? offset + 4 : offset;
    const time = captureRate < 0 ? frameView.getFloat32(offset, true) : i * captureRate;
    const position = new THREE.Vector3(
      frameView.getFloat32(dataOffset, true),
      frameView.getFloat32(dataOffset + 4, true),
      frameView.getFloat32(dataOffset + 8, true),
    );
    const qw = frameView.getFloat32(dataOffset + 12, true);
    const qx = frameView.getFloat32(dataOffset + 16, true);
    const qy = frameView.getFloat32(dataOffset + 20, true);
    const qz = frameView.getFloat32(dataOffset + 24, true);
    const rotation = new THREE.Quaternion(qx, qy, qz, qw).normalize();

    frames.push({ time, position, rotation });
  }

  return {
    kind: 'full',
    frameCount,
    captureRate,
    type,
    duration: Math.max(0, frames[frames.length - 1].time),
    frames,
  };
}

function readFloatChannel(channelNode, header = readAnimationChannelHeader(channelNode)) {
  if (!header) {
    return null;
  }

  const { frameBytes, frameCount, captureRate, type } = header;
  if (frameCount < 1) {
    return null;
  }

  const frameSize = (captureRate < 0 ? 4 : 0) + 4;
  if (frameBytes.byteLength < frameSize * frameCount) {
    return null;
  }

  const frameView = new DataView(frameBytes.buffer, frameBytes.byteOffset, frameBytes.byteLength);
  const frames = [];

  for (let i = 0; i < frameCount; i += 1) {
    const offset = i * frameSize;
    const dataOffset = captureRate < 0 ? offset + 4 : offset;
    frames.push({
      time: captureRate < 0 ? frameView.getFloat32(offset, true) : i * captureRate,
      value: frameView.getFloat32(dataOffset, true),
    });
  }

  return {
    kind: 'float',
    frameCount,
    captureRate,
    type,
    duration: Math.max(0, frames[frames.length - 1].time),
    frames,
  };
}

function readQuaternionChannel(channelNode, header = readAnimationChannelHeader(channelNode)) {
  if (!header) {
    return null;
  }

  const { frameBytes, frameCount, captureRate, type } = header;
  if (frameCount < 1) {
    return null;
  }

  const frameSize = (captureRate < 0 ? 4 : 0) + 16;
  if (frameBytes.byteLength < frameSize * frameCount) {
    return null;
  }

  const frameView = new DataView(frameBytes.buffer, frameBytes.byteOffset, frameBytes.byteLength);
  const frames = [];

  for (let i = 0; i < frameCount; i += 1) {
    const offset = i * frameSize;
    const dataOffset = captureRate < 0 ? offset + 4 : offset;
    const qw = frameView.getFloat32(dataOffset, true);
    const qx = frameView.getFloat32(dataOffset + 4, true);
    const qy = frameView.getFloat32(dataOffset + 8, true);
    const qz = frameView.getFloat32(dataOffset + 12, true);

    frames.push({
      time: captureRate < 0 ? frameView.getFloat32(offset, true) : i * captureRate,
      rotation: new THREE.Quaternion(qx, qy, qz, qw).normalize(),
    });
  }

  return {
    kind: 'quat',
    frameCount,
    captureRate,
    type,
    duration: Math.max(0, frames[frames.length - 1].time),
    frames,
  };
}

function readVectorChannel(channelNode, header = readAnimationChannelHeader(channelNode)) {
  if (!header) {
    return null;
  }

  const { frameBytes, frameCount, captureRate, type } = header;
  if (frameCount < 1) {
    return null;
  }

  const frameSize = (captureRate < 0 ? 4 : 0) + 12;
  if (frameBytes.byteLength < frameSize * frameCount) {
    return null;
  }

  const frameView = new DataView(frameBytes.buffer, frameBytes.byteOffset, frameBytes.byteLength);
  const frames = [];

  for (let i = 0; i < frameCount; i += 1) {
    const offset = i * frameSize;
    const dataOffset = captureRate < 0 ? offset + 4 : offset;
    frames.push({
      time: captureRate < 0 ? frameView.getFloat32(offset, true) : i * captureRate,
      position: new THREE.Vector3(
        frameView.getFloat32(dataOffset, true),
        frameView.getFloat32(dataOffset + 4, true),
        frameView.getFloat32(dataOffset + 8, true),
      ),
    });
  }

  return {
    kind: 'vector',
    frameCount,
    captureRate,
    type,
    duration: Math.max(0, frames[frames.length - 1].time),
    frames,
  };
}

class CompoundAnimationController {
  constructor(clips, byPartName) {
    this.clips = clips;
    this.byPartName = byPartName;
    this.bindTransforms = readBindTransforms(byPartName);
    this.activeClipIndex = Math.max(0, clips.indexOf(clips.reduce((best, clip) => (clip.duration > best.duration ? clip : best), clips[0])));
    this.activeClip = clips[this.activeClipIndex];
    this.clipCount = clips.length;
    this.time = 0;
    this.direction = 1;
    this.playing = false;
    this.loop = true;
    this.pingPong = false;
    this.apply();
  }

  play() {
    this.playing = true;
  }

  selectClip(index) {
    const nextIndex = clamp(Math.floor(index), 0, this.clips.length - 1);
    if (nextIndex === this.activeClipIndex) {
      return;
    }

    this.activeClipIndex = nextIndex;
    this.activeClip = this.clips[this.activeClipIndex];
    this.time = 0;
    this.direction = 1;
    this.apply();
  }

  stop() {
    this.playing = false;
  }

  restart() {
    this.time = 0;
    this.direction = 1;
    this.playing = true;
    this.apply();
  }

  reset() {
    this.time = 0;
    this.direction = 1;
    this.playing = false;
    this.apply();
  }

  update(dt) {
    if (!this.playing || !this.activeClip) {
      return;
    }

    this.time += dt * this.direction;
    this.constrainTime();

    this.apply();
  }

  constrainTime() {
    const duration = this.activeClip?.duration || 0;
    if (duration <= 0) {
      this.time = 0;
      this.playing = false;
      return;
    }

    if (this.pingPong) {
      while (this.time > duration || this.time < 0) {
        if (this.time > duration) {
          this.time = duration - (this.time - duration);
          this.direction = -1;
        } else if (this.time < 0) {
          this.time = -this.time;
          this.direction = 1;
          if (!this.loop) {
            this.time = 0;
            this.playing = false;
            break;
          }
        }
      }
      return;
    }

    if (this.loop) {
      this.time %= duration;
      if (this.time < 0) {
        this.time += duration;
      }
    } else if (this.time >= duration) {
      this.time = duration;
      this.playing = false;
    }
  }

  apply() {
    if (!this.activeClip) {
      return;
    }

    for (const track of this.activeClip.tracks) {
      const object = this.byPartName.get(track.targetName);
      if (!object) {
        continue;
      }

      if (track.targetType === 'object' && track.channel.kind === 'full') {
        const sample = sampleTransformChannel(track.channel, this.time);
        applyObjectFullTransform(object, sample, this.bindTransforms.get(track.targetName));
      } else if (track.channel.kind === 'full') {
        const sample = sampleTransformChannel(track.channel, this.time);
        applyFullJointLocalTransform(object, track.joint, sample);
      } else if (track.channel.kind === 'float') {
        const sample = sampleFloatChannel(track.channel, this.time, track.joint);
        applyFloatJointLocalTransform(object, track.joint, sample.value);
      } else if (track.channel.kind === 'quat') {
        const sample = sampleQuaternionChannel(track.channel, this.time);
        applyQuaternionJointLocalTransform(object, track.joint, sample.rotation);
      } else if (track.channel.kind === 'vector') {
        const sample = sampleVectorChannel(track.channel, this.time);
        applyVectorJointLocalTransform(object, track.joint, sample.position);
      }
    }
  }
}

function readBindTransforms(byPartName) {
  const binds = new Map();

  for (const [name, object] of byPartName) {
    object.updateMatrix();
    const position = new THREE.Vector3();
    const rotation = new THREE.Quaternion();
    const scale = new THREE.Vector3();
    object.matrix.decompose(position, rotation, scale);
    binds.set(name, { position, rotation, scale });
  }

  return binds;
}

function sampleTransformChannel(channel, time) {
  const frames = channel.frames;
  if (frames.length === 1 || time <= frames[0].time) {
    return frames[0];
  }

  const last = frames[frames.length - 1];
  if (time >= last.time) {
    return last;
  }

  let nextIndex = 1;
  while (nextIndex < frames.length && frames[nextIndex].time < time) {
    nextIndex += 1;
  }

  const previous = frames[nextIndex - 1];
  const next = frames[nextIndex];
  const span = Math.max(next.time - previous.time, 0.000001);
  const ratio = (time - previous.time) / span;

  return {
    time,
    position: previous.position.clone().lerp(next.position, ratio),
    rotation: previous.rotation.clone().slerp(next.rotation, ratio),
  };
}

function sampleFloatChannel(channel, time, joint) {
  const frames = channel.frames;
  if (frames.length === 1 || time <= frames[0].time) {
    return frames[0];
  }

  const last = frames[frames.length - 1];
  if (time >= last.time) {
    return last;
  }

  let nextIndex = 1;
  while (nextIndex < frames.length && frames[nextIndex].time < time) {
    nextIndex += 1;
  }

  const previous = frames[nextIndex - 1];
  const next = frames[nextIndex];
  const span = Math.max(next.time - previous.time, 0.000001);
  const ratio = (time - previous.time) / span;
  const value = joint.type === 'revolute'
    ? interpolateArc(previous.value, next.value, ratio)
    : lerp(previous.value, next.value, ratio);

  return { time, value };
}

function sampleQuaternionChannel(channel, time) {
  const frames = channel.frames;
  if (frames.length === 1 || time <= frames[0].time) {
    return frames[0];
  }

  const last = frames[frames.length - 1];
  if (time >= last.time) {
    return last;
  }

  let nextIndex = 1;
  while (nextIndex < frames.length && frames[nextIndex].time < time) {
    nextIndex += 1;
  }

  const previous = frames[nextIndex - 1];
  const next = frames[nextIndex];
  const span = Math.max(next.time - previous.time, 0.000001);
  const ratio = (time - previous.time) / span;

  return {
    time,
    rotation: previous.rotation.clone().slerp(next.rotation, ratio),
  };
}

function sampleVectorChannel(channel, time) {
  const frames = channel.frames;
  if (frames.length === 1 || time <= frames[0].time) {
    return frames[0];
  }

  const last = frames[frames.length - 1];
  if (time >= last.time) {
    return last;
  }

  let nextIndex = 1;
  while (nextIndex < frames.length && frames[nextIndex].time < time) {
    nextIndex += 1;
  }

  const previous = frames[nextIndex - 1];
  const next = frames[nextIndex];
  const span = Math.max(next.time - previous.time, 0.000001);
  const ratio = (time - previous.time) / span;

  return {
    time,
    position: previous.position.clone().lerp(next.position, ratio),
  };
}

function applyObjectFullTransform(object, sample, bind) {
  if (!bind) {
    return;
  }

  const position = bind.position.clone().add(sample.position.clone().applyQuaternion(bind.rotation));
  const rotation = bind.rotation.clone().multiply(sample.rotation);
  object.matrix.compose(position, rotation, bind.scale);
  object.matrixAutoUpdate = false;
  object.matrixWorldNeedsUpdate = true;
}

function applyFullJointLocalTransform(object, joint, sample) {
  const relOrientation = matrix4FromMatrix3(joint.relOrientation);
  const rotation = new THREE.Matrix4().makeRotationFromQuaternion(sample.rotation).multiply(relOrientation);

  if (joint.type === 'revolute' || joint.type === 'prismatic' || joint.type === 'spherical') {
    const childPoint = joint.childPoint.clone().applyMatrix4(rotation);
    const translation = joint.parentPoint.clone().add(sample.position).sub(childPoint);
    object.matrix.copy(matrix4FromRotationMatrixTranslation(rotation, translation));
  } else {
    const translation = joint.relPosition.clone().add(sample.position);
    object.matrix.copy(matrix4FromRotationMatrixTranslation(rotation, translation));
  }

  object.matrixAutoUpdate = false;
  object.matrixWorldNeedsUpdate = true;
}

function applyQuaternionJointLocalTransform(object, joint, rotationValue) {
  if (joint.type !== 'spherical') {
    return;
  }

  const relOrientation = matrix4FromMatrix3(joint.relOrientation);
  const rotation = new THREE.Matrix4().makeRotationFromQuaternion(rotationValue).multiply(relOrientation);
  const childPoint = joint.childPoint.clone().applyMatrix4(rotation);
  const translation = joint.parentPoint.clone().sub(childPoint);
  object.matrix.copy(matrix4FromRotationMatrixTranslation(rotation, translation));
  object.matrixAutoUpdate = false;
  object.matrixWorldNeedsUpdate = true;
}

function applyVectorJointLocalTransform(object, joint, position) {
  if (joint.type !== 'translational') {
    return;
  }

  const relOrientation = matrix4FromMatrix3(joint.relOrientation);
  const translation = joint.relPosition.clone().add(position);
  object.matrix.copy(matrix4FromRotationMatrixTranslation(relOrientation, translation));
  object.matrixAutoUpdate = false;
  object.matrixWorldNeedsUpdate = true;
}

function applyFloatJointLocalTransform(object, joint, value) {
  const relOrientation = matrix4FromMatrix3(joint.relOrientation);

  if (joint.type === 'revolute') {
    const axis = joint.axis.clone();
    if (axis.lengthSq() <= 0.000001) {
      axis.set(0, 1, 0);
    } else {
      axis.normalize();
    }

    const rotation = new THREE.Matrix4().makeRotationAxis(axis, value).multiply(relOrientation);
    const childPoint = joint.childPoint.clone().applyMatrix4(rotation);
    const translation = joint.parentPoint.clone().sub(childPoint);
    object.matrix.copy(matrix4FromRotationMatrixTranslation(rotation, translation));
  } else if (joint.type === 'prismatic') {
    const axis = joint.axis.clone();
    if (axis.lengthSq() > 0.000001) {
      axis.normalize().multiplyScalar(value);
    }

    const childPoint = joint.childPoint.clone().applyMatrix4(relOrientation);
    const translation = joint.parentPoint.clone().add(axis).sub(childPoint);
    object.matrix.copy(matrix4FromRotationMatrixTranslation(relOrientation, translation));
  }

  object.matrixAutoUpdate = false;
  object.matrixWorldNeedsUpdate = true;
}

function interpolateArc(base, next, ratio) {
  let adjustedNext = next;
  if (adjustedNext - base < -Math.PI) {
    adjustedNext += Math.PI * 2;
  } else if (adjustedNext - base > Math.PI) {
    adjustedNext -= Math.PI * 2;
  }

  return base + (adjustedNext - base) * ratio;
}

function jointKey(parent, child) {
  return `${parent}\n${child}`;
}

function readFixedString(view, offset, maxLength) {
  const bytes = new Uint8Array(view.buffer, view.byteOffset + offset, maxLength);
  let end = 0;

  while (end < bytes.length && bytes[end] !== 0) {
    end += 1;
  }

  return new TextDecoder('ascii').decode(bytes.slice(0, end));
}

function readCString(bytes, offset, maxLength) {
  let end = offset;
  const limit = offset + maxLength;
  while (end < limit && bytes[end] !== 0) {
    end += 1;
  }
  return new TextDecoder('ascii').decode(bytes.slice(offset, end));
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

function matrix4FromRotationMatrixTranslation(rotation, translation) {
  const out = rotation.clone();
  out.setPosition(translation);
  return out;
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
  const embeddedImageCanvases = loadEmbeddedImageCanvases(root);
  const textureLibrary = root['Texture library']?.children || root.embeddedAssets?.children?.['Texture library']?.children || {};
  const textureCanvases = new Map(embeddedImageCanvases);

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

function loadEmbeddedImageCanvases(root) {
  const imageRoot = (root.embeddedImages?.children || root.embeddedImages?.childrenList) ? root.embeddedImages : null;
  const imageNodes = imageRoot?.childrenList || Object.values(imageRoot?.children || {});
  const textureCanvases = new Map();

  for (const imageNode of imageNodes) {
    if (!imageNode?.attributes) {
      continue;
    }

    const decoded = decodeEmbeddedImageNode(imageNode);
    if (decoded) {
      textureCanvases.set(decoded.dataset.textureName, decoded);
    }
  }

  return textureCanvases;
}

function decodeEmbeddedImageNode(imageNode) {
  const attrs = imageNode.attributes || {};
  const format = (attrs.format || '').toLowerCase();
  const dataNode = findChildCI(imageNode, 'Image BMP') || findChildCI(imageNode, 'BMP') || findChildCI(imageNode, 'Image');

  if (format !== 'bmp' || !dataNode?.value) {
    return null;
  }

  const decoded = decodeBmp32(dataNode.value);
  if (!decoded) {
    return null;
  }

  return makeCanvas(
    attrs.name || imageNode.name || 'embedded.bmp',
    Number(attrs.width) || decoded.width,
    Number(attrs.height) || decoded.height,
    decoded.rgba,
    attrs.alpha === 'source' || attrs.alpha === 'luminance' || hasNonOpaqueAlpha(decoded.rgba),
  );
}

function decodeBmp32(bytes) {
  if (!bytes || bytes.byteLength < 54 || bytes[0] !== 0x42 || bytes[1] !== 0x4d) {
    return null;
  }

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const pixelOffset = view.getUint32(10, true);
  const dibSize = view.getUint32(14, true);
  if (dibSize < 40) {
    return null;
  }

  const width = view.getInt32(18, true);
  const signedHeight = view.getInt32(22, true);
  const height = Math.abs(signedHeight);
  const bitCount = view.getUint16(28, true);
  const compression = view.getUint32(30, true);
  if (width <= 0 || height <= 0 || bitCount !== 32 || compression !== 0) {
    return null;
  }

  const topDown = signedHeight < 0;
  const rowSize = width * 4;
  const rgba = new Uint8ClampedArray(width * height * 4);

  for (let y = 0; y < height; y += 1) {
    const srcY = topDown ? y : height - 1 - y;
    const rowOffset = pixelOffset + srcY * rowSize;
    for (let x = 0; x < width; x += 1) {
      const src = rowOffset + x * 4;
      const dst = (y * width + x) * 4;
      rgba[dst] = bytes[src + 2] || 0;
      rgba[dst + 1] = bytes[src + 1] || 0;
      rgba[dst + 2] = bytes[src] || 0;
      rgba[dst + 3] = bytes[src + 3] ?? 255;
    }
  }

  return { width, height, rgba };
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

const COMPOUND_PARTICLE_SIZE_MULTIPLIER = 10;

function createParticleObject(parameters, textureCanvases, name) {
  const previewParameters = scaleParticlePreviewSizes(parameters, COMPOUND_PARTICLE_SIZE_MULTIPLIER);
  const textureCanvas = findTextureCanvas(textureCanvases, previewParameters.textureName);
  const preview = new ParticlePreview(previewParameters, textureCanvas);
  preview.points.name = name;
  preview.points.userData.particlePreview = preview;
  return preview.points;
}

function scaleParticlePreviewSizes(parameters, multiplier) {
  return {
    ...parameters,
    particleSize: parameters.particleSize * multiplier,
    particleSizeVelocity: parameters.particleSizeVelocity * multiplier,
  };
}

class ParticlePreview {
  constructor(parameters, textureCanvas) {
    this.parameters = parameters;
    this.particles = [];
    this.spawnAccumulator = Math.max(0, parameters.initialParticleCount);
    this.createdParticles = 0;
    this.elapsed = 0;
    this.emitterLifetime = parameters.lifetime;
    this.maxParticles = computeMaxParticles(parameters);
    this.positions = new Float32Array(this.maxParticles * 3);
    this.colors = new Float32Array(this.maxParticles * 4);
    this.sizes = new Float32Array(this.maxParticles);
    this.geometry = new THREE.BufferGeometry();
    this.geometry.setAttribute('position', new THREE.BufferAttribute(this.positions, 3).setUsage(THREE.DynamicDrawUsage));
    this.geometry.setAttribute('particleColor', new THREE.BufferAttribute(this.colors, 4).setUsage(THREE.DynamicDrawUsage));
    this.geometry.setAttribute('particleSize', new THREE.BufferAttribute(this.sizes, 1).setUsage(THREE.DynamicDrawUsage));
    this.geometry.setDrawRange(0, 0);
    this.texture = makeParticleTexture(textureCanvas);
    this.material = makeParticleMaterial(this.texture, parameters);
    this.points = new THREE.Points(this.geometry, this.material);
    this.points.frustumCulled = false;
    this.restart();
  }

  restart() {
    this.particles.length = 0;
    this.spawnAccumulator = Math.max(0, this.parameters.initialParticleCount);
    this.createdParticles = 0;
    this.elapsed = 0;
    this.emitterLifetime = this.parameters.lifetime;
  }

  update(dt) {
    const p = this.parameters;
    this.elapsed += dt;

    if (dt > 0 && p.lifetime > 0) {
      this.emitterLifetime -= dt;
    }

    for (let i = this.particles.length - 1; i >= 0; i -= 1) {
      const particle = this.particles[i];
      updateParticle(particle, p, dt);
      if (p.particleLifetime > 0 && particle.lifetime <= 0) {
        this.particles.splice(i, 1);
      }
    }

    const canCreate = p.lifetime <= 0 || this.emitterLifetime > 0;
    if (canCreate) {
      this.spawnAccumulator += Math.max(0, p.frequency) * dt;
      let count = Math.floor(this.spawnAccumulator);
      this.spawnAccumulator -= count;

      if (p.maxParticleCount > 0) {
        count = Math.min(count, Math.max(0, p.maxParticleCount - this.createdParticles));
      }

      for (let i = 0; i < count; i += 1) {
        this.spawnParticle();
      }
    }

    this.syncGeometry();
  }

  spawnParticle() {
    const p = this.parameters;
    if (this.particles.length >= this.maxParticles) {
      this.particles.shift();
    }

    const direction = makeEmitterDirection(p);
    const randomVelocity = Math.random() * Math.max(0, p.particleVelocityRandomizer);
    const velocity = p.particleVelocity + p.particleVelocity * randomVelocity;
    const position = new THREE.Vector3();

    if (p.particlePositionRandomizer) {
      position.x += fRand() * p.particlePositionRandomizer;
      position.y += fRand() * p.particlePositionRandomizer;
      position.z += fRand() * p.particlePositionRandomizer;
    }

    this.particles.push({
      position,
      direction,
      velocity,
      size: p.particleSize,
      lifetime: p.particleLifetime,
      age: 0,
    });
    this.createdParticles += 1;
  }

  syncGeometry() {
    const count = Math.min(this.particles.length, this.maxParticles);
    const p = this.parameters;

    for (let i = 0; i < count; i += 1) {
      const particle = this.particles[i];
      const color = particleColor(p, particle);
      const pi = i * 3;
      const ci = i * 4;
      this.positions[pi] = particle.position.x;
      this.positions[pi + 1] = particle.position.y;
      this.positions[pi + 2] = particle.position.z;
      this.colors[ci] = color.r;
      this.colors[ci + 1] = color.g;
      this.colors[ci + 2] = color.b;
      this.colors[ci + 3] = color.a;
      this.sizes[i] = Math.max(0.01, particle.size);
    }

    this.geometry.setDrawRange(0, count);
    this.geometry.attributes.position.needsUpdate = true;
    this.geometry.attributes.particleColor.needsUpdate = true;
    this.geometry.attributes.particleSize.needsUpdate = true;
  }
}

function updateParticle(particle, parameters, dt) {
  if (parameters.particleTwistVelocity !== 0) {
    const twist = parameters.particleTwistVelocity * dt;
    particle.direction.x -= particle.direction.y * twist;
    particle.direction.y += particle.direction.x * twist;
    particle.direction.normalize();
  }

  if (parameters.particleSizeVelocity) {
    particle.size += parameters.particleSizeVelocity * dt;
  }

  particle.direction.x += parameters.gravity.x * dt;
  particle.direction.y += parameters.gravity.y * dt;
  particle.direction.z += parameters.gravity.z * dt;
  particle.position.addScaledVector(particle.direction, particle.velocity * dt);
  particle.lifetime -= dt;
  particle.age += dt;
}

function makeEmitterDirection(parameters) {
  const damp = parameters.emitterNozzleDamp;
  const degenerateDamp = damp.lengthSq() === 0;
  const direction = parameters.emitterDirection.clone();

  if (degenerateDamp) {
    return direction.lengthSq() > 0 ? direction.normalize() : new THREE.Vector3(0, 0, 1);
  }

  direction.multiplyScalar(parameters.emitterNozzleSize);
  direction.x += fRand() * damp.x;
  direction.y += fRand() * damp.y;
  direction.z += fRand() * damp.z;
  return direction.lengthSq() > 0 ? direction.normalize() : new THREE.Vector3(0, 0, 1);
}

function particleColor(parameters, particle) {
  const frameMax = PSP_NUM_COLOR_KEYS - 1;
  const life = parameters.particleLifetime > 0 ? parameters.particleLifetime : Math.max(1, particle.age + particle.lifetime);
  const t = clamp(particle.age / life, 0, 1);
  const frame = t * frameMax;
  const i0 = Math.min(frameMax, Math.floor(frame));
  const i1 = Math.min(frameMax, i0 + 1);
  const mix = frame - i0;
  const c0 = parameters.colorFrames[i0];
  const c1 = parameters.colorFrames[i1];

  return {
    r: lerp(c0.r, c1.r, mix),
    g: lerp(c0.g, c1.g, mix),
    b: lerp(c0.b, c1.b, mix),
    a: lerp(c0.a, c1.a, mix),
  };
}

function makeParticleTexture(textureCanvas) {
  if (textureCanvas) {
    const texture = new THREE.CanvasTexture(textureCanvas);
    texture.magFilter = THREE.NearestFilter;
    texture.minFilter = THREE.LinearMipmapLinearFilter;
    texture.generateMipmaps = true;
    texture.colorSpace = THREE.SRGBColorSpace;
    return texture;
  }

  const canvasTexture = document.createElement('canvas');
  canvasTexture.width = 32;
  canvasTexture.height = 32;
  const ctx = canvasTexture.getContext('2d');
  const gradient = ctx.createRadialGradient(16, 16, 0, 16, 16, 16);
  gradient.addColorStop(0, 'rgba(255,255,255,1)');
  gradient.addColorStop(0.55, 'rgba(255,255,255,0.7)');
  gradient.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = gradient;
  ctx.fillRect(0, 0, 32, 32);
  return new THREE.CanvasTexture(canvasTexture);
}

function makeParticleMaterial(texture, parameters) {
  const material = new THREE.ShaderMaterial({
    uniforms: {
      pointTexture: { value: texture },
    },
    vertexShader: `
      attribute vec4 particleColor;
      attribute float particleSize;
      varying vec4 vColor;

      void main() {
        vColor = particleColor;
        vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
        gl_PointSize = max(1.0, particleSize * (300.0 / max(1.0, -mvPosition.z)));
        gl_Position = projectionMatrix * mvPosition;
      }
    `,
    fragmentShader: `
      uniform sampler2D pointTexture;
      varying vec4 vColor;

      void main() {
        vec4 texel = texture2D(pointTexture, gl_PointCoord);
        gl_FragColor = vec4(texel.rgb * vColor.rgb, texel.a);
        if (texel.a <= 0.01) {
          discard;
        }
      }
    `,
    transparent: true,
    depthWrite: false,
  });

  applyParticleBlendMode(material, parameters);
  return material;
}

function applyParticleBlendMode(material, parameters) {
  if (parameters.srcBlend === D3DBLEND_ONE && parameters.dstBlend === D3DBLEND_ONE) {
    material.blending = THREE.CustomBlending;
    material.blendEquation = THREE.AddEquation;
    material.blendSrc = THREE.OneFactor;
    material.blendDst = THREE.OneFactor;
    material.blendSrcAlpha = THREE.OneFactor;
    material.blendDstAlpha = THREE.OneFactor;
    return;
  }

  if (parameters.srcBlend === D3DBLEND_SRCALPHA && parameters.dstBlend === D3DBLEND_INVSRCALPHA) {
    material.blending = THREE.NormalBlending;
    return;
  }

  if (parameters.srcBlend === D3DBLEND_ONE && parameters.dstBlend === D3DBLEND_ZERO) {
    material.blending = THREE.NoBlending;
    return;
  }

  material.blending = THREE.AdditiveBlending;
}

function computeMaxParticles(parameters) {
  let maxPossible;

  if (parameters.particleLifetime > 0) {
    maxPossible = 1.25 * parameters.frequency * parameters.particleLifetime + parameters.initialParticleCount;
  } else if (parameters.lifetime > 0) {
    maxPossible = 1.25 * (1 + parameters.frequency) + parameters.initialParticleCount;
  } else {
    maxPossible = 128 + parameters.initialParticleCount;
  }

  if (parameters.maxParticleCount > 0) {
    maxPossible = Math.min(maxPossible, parameters.maxParticleCount);
  }

  return Math.max(16, Math.min(20000, Math.ceil(maxPossible)));
}

function setActiveMesh(mesh, textureCanvases) {
  if (activeMesh) {
    scene.remove(activeMesh);
    disposeObject(activeMesh);
  }

  activeMesh = mesh;
  activeTextureCanvases = textureCanvases;
  activeParticleSystems = collectParticleSystems(activeMesh);
  activeAnimationController = activeMesh.userData?.animationController || null;
  updateAnimationButtons();
  updateAnimationPanel();
  scene.add(activeMesh);
}

function collectParticleSystems(root) {
  const systems = [];
  root.traverse((object) => {
    if (object.userData?.particlePreview) {
      systems.push(object.userData.particlePreview);
    }
  });
  return systems;
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
  const now = performance.now();
  const dt = Math.min(0.05, Math.max(0, (now - lastTime) / 1000));
  lastTime = now;

  for (const particleSystem of activeParticleSystems) {
    particleSystem.update(dt);
  }

  if (activeAnimationController) {
    activeAnimationController.update(dt);
    updateAnimationButtons();
    updateAnimationPanel();
  }

  renderer.render(scene, camera);
}

function setWireframe(enabled) {
  if (!activeMesh) {
    return;
  }

  activeMesh.traverse((object) => {
    if (object.isMesh && !object.userData?.hardpointMarker) {
      object.material.wireframe = enabled;
    }
  });
}

function updateStats(stats, textureCanvases) {
  const particleCount = stats.particles || 0;
  const animationCount = stats.animations || 0;
  const hardpointCount = stats.hardpoints || 0;
  statsLine.textContent = `${stats.vertices} vertices | ${stats.faces} faces | ${stats.materials} materials | ${textureCanvases.size} textures | ${animationCount} animation clips | ${hardpointCount} hardpoints | ${particleCount} particle systems`;
}

function updateAnimationButtons() {
  const hasAnimation = Boolean(activeAnimationController);
  animStartButton.disabled = !hasAnimation || activeAnimationController.playing;
  animStopButton.disabled = !hasAnimation || !activeAnimationController.playing;
  animRestartButton.disabled = !hasAnimation;
  animResetButton.disabled = !hasAnimation;
  animLoopToggle.disabled = !hasAnimation;
  animPingPongToggle.disabled = !hasAnimation;

  if (hasAnimation) {
    animLoopToggle.checked = activeAnimationController.loop;
    animPingPongToggle.checked = activeAnimationController.pingPong;
  }
}

function updateAnimationPanel() {
  if (!activeAnimationController) {
    animationPanel.hidden = true;
    animationSelect.replaceChildren();
    animationMeta.textContent = '';
    return;
  }

  const controller = activeAnimationController;
  const clip = controller.activeClip;
  animationPanel.hidden = false;
  animationTitle.textContent = `${controller.clipCount} animation ${controller.clipCount === 1 ? 'clip' : 'clips'} available`;

  if (animationSelect.options.length !== controller.clips.length) {
    animationSelect.replaceChildren(...controller.clips.map((item, index) => {
      const option = document.createElement('option');
      option.value = String(index);
      option.textContent = `${item.name} (${formatSeconds(item.duration)})`;
      return option;
    }));
  }

  animationSelect.value = String(controller.activeClipIndex);
  animationMeta.textContent = clip
    ? `${controller.playing ? 'Playing' : 'Stopped'} ${clip.name} at ${formatSeconds(controller.time)} / ${formatSeconds(clip.duration)} with ${clip.tracks.length} tracks.`
    : 'No active animation clip.';
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

function fRand() {
  return Math.random() * 2 - 1;
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function formatSeconds(value) {
  return `${Math.max(0, value).toFixed(2)}s`;
}
