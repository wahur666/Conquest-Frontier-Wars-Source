import * as THREE from 'three';
import { loadUtfFile, parseUtfXmlString } from './utfParser.js';
import { findTextureCanvas, loadTextureCanvases } from './utfTextureUtils.js';

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
const DEFAULT_SAMPLE = '../xml_dump/blast.pte.xml';

const fileInput = document.querySelector('#pte-file');
const openButton = document.querySelector('#open-file');
const sampleButton = document.querySelector('#load-sample');
const restartButton = document.querySelector('#restart');
const resetButton = document.querySelector('#reset-view');
const statusLine = document.querySelector('#status');
const statsLine = document.querySelector('#stats');
const canvas = document.querySelector('#viewport');
const parameterPanel = document.querySelector('#parameter-panel');

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
  distance: 1000,
};

scene.add(new THREE.HemisphereLight(0xffffff, 0x273044, 1.25));

const keyLight = new THREE.DirectionalLight(0xffffff, 1.6);
keyLight.position.set(8, 12, 10);
scene.add(keyLight);

const grid = new THREE.GridHelper(1000, 20, 0x46505f, 0x2a303a);
scene.add(grid);

const axes = new THREE.AxesHelper(150);
scene.add(axes);

let activeSystem = null;
let currentParameters = null;
let currentTextureCanvas = null;
let currentDisplayName = '';
let lastTime = performance.now();

openButton.addEventListener('click', () => fileInput.click());
sampleButton.addEventListener('click', loadSample);
restartButton.addEventListener('click', () => activeSystem?.restart());
resetButton.addEventListener('click', frameParticleSystem);
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
    loadParticleRoot(utfRoot, file.name);
  } catch (error) {
    console.error(error);
    setStatus(error instanceof Error ? error.message : String(error), true);
  } finally {
    event.target.value = '';
  }
}

async function loadSample() {
  setStatus('Loading sample blast.pte.xml...');

  try {
    const response = await fetch(DEFAULT_SAMPLE);
    if (!response.ok) {
      throw new Error(`Could not fetch ${DEFAULT_SAMPLE}. Serve the repo root over HTTP and try again.`);
    }

    loadParticleRoot(parseUtfXmlString(await response.text()), 'blast.pte.xml');
  } catch (error) {
    console.error(error);
    setStatus(error instanceof Error ? error.message : String(error), true);
  }
}

function loadParticleRoot(utfRoot, displayName) {
  const rootNode = findUtfRootNode(utfRoot);

  if (isUnifiedParticleRoot(rootNode)) {
    loadUnifiedParticleRoot(rootNode, displayName);
    return;
  }

  const children = rootNode?.children;

  if (!children) {
    throw new Error('This XML does not contain a UTF root directory.');
  }

  const { parameters, textureChildren } = loadParticleParameters(children);
  const textureCanvases = loadTextureCanvases(textureChildren);
  const textureCanvas = findTextureCanvas(textureCanvases, parameters.textureName);

  currentParameters = parameters;
  currentTextureCanvas = textureCanvas;
  currentDisplayName = displayName;
  setActiveSystem(new ParticlePreview(currentParameters, currentTextureCanvas));
  frameParticleSystem();
  setStatus(`Loaded ${displayName}`);
  updateStats(displayName, currentParameters, currentTextureCanvas);
  renderParameterPanel();
}

function loadUnifiedParticleRoot(rootNode, displayName) {
  const parameters = parseUnifiedParticleParameters(rootNode);
  const embeddedAssets = findXmlChild(rootNode, 'embeddedAssets');
  const textureChildren = rootNode.children || embeddedAssets?.children || {};
  const textureCanvases = loadTextureCanvases(textureChildren);
  const textureCanvas = findTextureCanvas(textureCanvases, parameters.textureName);

  currentParameters = parameters;
  currentTextureCanvas = textureCanvas;
  currentDisplayName = displayName;
  setActiveSystem(new ParticlePreview(currentParameters, currentTextureCanvas));
  frameParticleSystem();
  setStatus(`Loaded ${displayName}`);
  updateStats(displayName, currentParameters, currentTextureCanvas);
  renderParameterPanel();
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

  throw new Error('This XML does not contain ParticleSystemParameters or Particle Event/particle1.Def.');
}

function setActiveSystem(system) {
  if (activeSystem) {
    scene.remove(activeSystem.points);
    activeSystem.dispose();
  }

  activeSystem = system;
  scene.add(system.points);
}

function rebuildActiveSystem() {
  if (!currentParameters) {
    return;
  }

  setActiveSystem(new ParticlePreview(currentParameters, currentTextureCanvas));
  updateStats(currentDisplayName, currentParameters, currentTextureCanvas);
}

function renderParameterPanel() {
  if (!currentParameters) {
    parameterPanel.innerHTML = '<h2>Parameters</h2><div class="status">Load a particle system to inspect and edit values.</div>';
    return;
  }

  const p = currentParameters;
  parameterPanel.innerHTML = `
    <h2>Parameters</h2>
    <div class="section">
      <div class="section-title">Emitter</div>
      ${numberField('Initial particles', 'initialParticleCount', 1)}
      ${numberField('Max particles', 'maxParticleCount', 1)}
      ${numberField('Frequency', 'frequency', 0.1)}
      ${numberField('Lifetime', 'lifetime', 0.1)}
      ${vectorFields('Direction', 'emitterDirection', 0.01)}
      ${numberField('Nozzle size', 'emitterNozzleSize', 0.1)}
      ${vectorFields('Nozzle damp', 'emitterNozzleDamp', 0.01)}
    </div>
    <div class="section">
      <div class="section-title">Particles</div>
      ${numberField('Particle lifetime', 'particleLifetime', 0.1)}
      ${numberField('Initial size', 'particleSize', 0.1)}
      ${numberField('Size velocity', 'particleSizeVelocity', 0.1)}
      ${numberField('Initial velocity', 'particleVelocity', 0.1)}
      ${numberField('Velocity random', 'particleVelocityRandomizer', 0.01)}
      ${numberField('Twist speed', 'particleTwistVelocity', 0.01)}
      ${numberField('Position random', 'particlePositionRandomizer', 0.1)}
      ${vectorFields('Gravity', 'gravity', 0.01)}
    </div>
    <div class="section">
      <div class="section-title">Rendering</div>
      ${textField('Texture', 'textureName')}
      ${numberField('Texture FPS', 'textureFps', 0.1)}
      ${selectField('Source blend', 'srcBlend')}
      ${selectField('Dest blend', 'dstBlend')}
      ${flagField('Inherit transform', PSP_F_RELATIVE_TRANSFORM)}
      ${flagField('Inherit velocity', PSP_F_RELATIVE_VELOCITY)}
      ${flagField('Use particle life', PSP_F_RENDER_PARTICLE_LIFE)}
      ${numberField('Bounding radius', 'boundingSphereRadius', 1)}
    </div>
    <div class="section">
      <div class="section-title">Color Keys</div>
      ${colorKeyRows(p)}
    </div>
  `;

  parameterPanel.querySelectorAll('[data-param]').forEach((input) => {
    input.addEventListener('change', onParameterInput);
  });
  parameterPanel.querySelectorAll('[data-flag]').forEach((input) => {
    input.addEventListener('change', onFlagInput);
  });
  parameterPanel.querySelectorAll('[data-color-index]').forEach((input) => {
    input.addEventListener('input', onColorInput);
    input.addEventListener('change', onColorInput);
  });
}

function onParameterInput(event) {
  const input = event.currentTarget;
  const path = input.dataset.param;
  const value = input.type === 'number' || input.tagName === 'SELECT'
    ? Number(input.value)
    : input.value;

  setPath(currentParameters, path, value);
  rebuildActiveSystem();
}

function onFlagInput(event) {
  const input = event.currentTarget;
  const bit = Number(input.dataset.flag);
  if (input.checked) {
    currentParameters.pspFlags |= bit;
  } else {
    currentParameters.pspFlags &= ~bit;
  }
  rebuildActiveSystem();
}

function onColorInput(event) {
  const input = event.currentTarget;
  const index = Number(input.dataset.colorIndex);
  const frame = currentParameters.colorFrames[index];

  if (input.dataset.colorKind === 'rgb') {
    const color = hexToRgb(input.value);
    frame.r = color.r;
    frame.g = color.g;
    frame.b = color.b;
  } else {
    frame.a = Number(input.value);
  }

  currentParameters.colorKeyFrameBits |= (1 << index);
  rebuildActiveSystem();
}

function numberField(label, path, step) {
  const value = Number(getPath(currentParameters, path) ?? 0);
  return `
    <div class="field">
      <label>${label}</label>
      <input data-param="${path}" type="number" step="${step}" value="${formatNumber(value)}">
    </div>
  `;
}

function textField(label, path) {
  const value = String(getPath(currentParameters, path) ?? '');
  return `
    <div class="field">
      <label>${label}</label>
      <input data-param="${path}" type="text" value="${escapeHtml(value)}">
    </div>
  `;
}

function vectorFields(label, path, step) {
  return `
    ${numberField(`${label} X`, `${path}.x`, step)}
    ${numberField(`${label} Y`, `${path}.y`, step)}
    ${numberField(`${label} Z`, `${path}.z`, step)}
  `;
}

function selectField(label, path) {
  const value = Number(getPath(currentParameters, path) ?? D3DBLEND_ONE);
  const options = D3D_BLEND_OPTIONS.map((option) => {
    const selected = option.value === value ? ' selected' : '';
    return `<option value="${option.value}"${selected}>${option.label}</option>`;
  }).join('');

  return `
    <div class="field">
      <label>${label}</label>
      <select data-param="${path}">${options}</select>
    </div>
  `;
}

function flagField(label, bit) {
  const checked = currentParameters.pspFlags & bit ? ' checked' : '';
  return `
    <div class="check">
      <input id="flag-${bit}" data-flag="${bit}" type="checkbox"${checked}>
      <label for="flag-${bit}">${label}</label>
    </div>
  `;
}

function colorKeyRows(parameters) {
  const indices = new Set([0, PSP_NUM_COLOR_KEYS - 1]);
  for (let i = 0; i < PSP_NUM_COLOR_KEYS; i += 1) {
    if (parameters.colorKeyFrameBits & (1 << i)) {
      indices.add(i);
    }
  }

  return Array.from(indices).sort((a, b) => a - b).map((index) => {
    const frame = parameters.colorFrames[index];
    return `
      <div class="color-row">
        <span>${index + 1}</span>
        <input data-color-index="${index}" data-color-kind="rgb" type="color" value="${rgbToHex(frame)}">
        <input data-color-index="${index}" data-color-kind="alpha" type="number" min="0" max="1" step="0.01" value="${formatNumber(frame.a)}">
      </div>
    `;
  }).join('');
}

const D3D_BLEND_OPTIONS = [
  { value: 1, label: 'ZERO' },
  { value: 2, label: 'ONE' },
  { value: 3, label: 'SRCCOLOR' },
  { value: 4, label: 'INVSRCCOLOR' },
  { value: 5, label: 'SRCALPHA' },
  { value: 6, label: 'INVSRCALPHA' },
  { value: 7, label: 'DESTALPHA' },
  { value: 8, label: 'INVDESTALPHA' },
  { value: 9, label: 'DESTCOLOR' },
  { value: 10, label: 'INVDESTCOLOR' },
  { value: 11, label: 'SRCALPHASAT' },
  { value: 12, label: 'BOTHSRCALPHA' },
];

function getPath(object, path) {
  return path.split('.').reduce((target, key) => target?.[key], object);
}

function setPath(object, path, value) {
  const parts = path.split('.');
  const final = parts.pop();
  const target = parts.reduce((item, key) => item[key], object);
  target[final] = value;
}

function rgbToHex(color) {
  const r = Math.round(clamp(color.r, 0, 1) * 255).toString(16).padStart(2, '0');
  const g = Math.round(clamp(color.g, 0, 1) * 255).toString(16).padStart(2, '0');
  const b = Math.round(clamp(color.b, 0, 1) * 255).toString(16).padStart(2, '0');
  return `#${r}${g}${b}`;
}

function hexToRgb(value) {
  const number = Number.parseInt(value.replace('#', ''), 16);
  return {
    r: ((number >> 16) & 0xff) / 255,
    g: ((number >> 8) & 0xff) / 255,
    b: (number & 0xff) / 255,
  };
}

function formatNumber(value) {
  if (!Number.isFinite(value)) {
    return '0';
  }
  return Number.isInteger(value) ? String(value) : String(Number(value.toFixed(4)));
}

function escapeHtml(value) {
  return value
    .replaceAll('&', '&amp;')
    .replaceAll('"', '&quot;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;');
}

function parseParticleSystemParameters(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let offset = 0;
  const parameters = {};

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

  if (vectorLengthSq(direction) <= 0.000001) {
    direction = { x: 1, y: 1, z: 1 };
  }

  parameters.lifetime = lifetime * 0.001;
  parameters.frequency = frequency;
  parameters.initialParticleCount = nParticles;
  parameters.maxParticleCount = maxParticles;
  parameters.emitterDirection = direction;
  parameters.emitterNozzleSize = nozzle;
  parameters.emitterNozzleDamp = nozzleDamp;
  parameters.gravity = { x: 0, y: 0, z: gravity * 1000 };
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
    const useParticleLifetime = view.getInt32(196, true);
    const fog = view.getInt32(200, true);

    if (srcBlend > 0) {
      parameters.srcBlend = srcBlend;
    }
    if (dstBlend > 0) {
      parameters.dstBlend = dstBlend;
    }
    parameters.gravity = {
      x: gravityVec.x * 1000,
      y: gravityVec.y * 1000,
      z: (gravityVec.z + gravity) * 1000,
    };
    if (useParticleLifetime) {
      parameters.pspFlags |= PSP_F_RENDER_PARTICLE_LIFE;
    }
    if (fog) {
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
  } else if (vectorLengthSq(colorVelocity) > 0) {
    const durationMs = partLife || lifetime || 1000;
    const frameStepMs = durationMs / PSP_NUM_COLOR_KEYS;
    let r = color.x;
    let g = color.y;
    let b = color.z;
    let a = alpha;

    parameters.colorFrames = [];
    for (let i = 0; i < PSP_NUM_COLOR_KEYS; i += 1) {
      parameters.colorFrames.push({
        r: clamp(r, 0, 1),
        g: clamp(g, 0, 1),
        b: clamp(b, 0, 1),
        a: clamp(a, 0, 1),
      });
      r += frameStepMs * 0.001 * colorVelocity.x;
      g += frameStepMs * 0.001 * colorVelocity.y;
      b += frameStepMs * 0.001 * colorVelocity.z;
      a += frameStepMs * 0.001 * alphaDecay;
    }
  } else {
    parameters.colorFrames = parameters.colorFrames.map(() => ({
      r: color.x,
      g: color.y,
      b: color.z,
      a: alpha,
    }));
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
    gravity: { x: 0, y: 0, z: 0 },
    emitterDirection: { x: 1, y: 1, z: 1 },
    emitterNozzleSize: 0,
    emitterNozzleDamp: { x: 0, y: 0, z: 0 },
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

function parseUnifiedParticleParameters(rootNode) {
  const parametersNode = findXmlChild(rootNode, 'parameters');
  if (!parametersNode) {
    throw new Error('Unified particle XML is missing <parameters>.');
  }

  const parameters = createDefaultParticleParameters();
  const rendering = findXmlChild(parametersNode, 'rendering');
  const emitter = findXmlChild(parametersNode, 'emitter');
  const particles = findXmlChild(parametersNode, 'particles');
  const colorFrames = findXmlChild(parametersNode, 'colorFrames');

  if (rendering) {
    parameters.textureName = attr(rendering, 'textureName', parameters.textureName);
    parameters.textureFps = parseNumberAttribute(rendering, 'textureFps', parameters.textureFps);
    parameters.srcBlend = parseIntegerAttribute(rendering, 'srcBlend', parameters.srcBlend);
    parameters.dstBlend = parseIntegerAttribute(rendering, 'dstBlend', parameters.dstBlend);
    parameters.boundingSphereRadius = parseNumberAttribute(rendering, 'boundingSphereRadius', parameters.boundingSphereRadius);
  }

  if (emitter) {
    parameters.initialParticleCount = parseNumberAttribute(emitter, 'initialParticleCount', parameters.initialParticleCount);
    parameters.maxParticleCount = parseIntegerAttribute(emitter, 'maxParticleCount', parameters.maxParticleCount);
    parameters.lifetime = parseNumberAttribute(emitter, 'lifetime', parameters.lifetime);
    parameters.frequency = parseNumberAttribute(emitter, 'frequency', parameters.frequency);
    parameters.emitterNozzleSize = parseNumberAttribute(emitter, 'nozzleSize', parameters.emitterNozzleSize);
    parameters.emitterDirection = parseUnifiedVector(findXmlChild(emitter, 'direction'), parameters.emitterDirection);
    parameters.emitterNozzleDamp = parseUnifiedVector(findXmlChild(emitter, 'nozzleDamp'), parameters.emitterNozzleDamp);
  }

  if (particles) {
    parameters.particleLifetime = parseNumberAttribute(particles, 'lifetime', parameters.particleLifetime);
    parameters.particlePositionRandomizer = parseNumberAttribute(particles, 'positionRandomizer', parameters.particlePositionRandomizer);
    parameters.particleVelocity = parseNumberAttribute(particles, 'velocity', parameters.particleVelocity);
    parameters.particleVelocityRandomizer = parseNumberAttribute(particles, 'velocityRandomizer', parameters.particleVelocityRandomizer);
    parameters.particleTwistVelocity = parseNumberAttribute(particles, 'twistVelocity', parameters.particleTwistVelocity);
    parameters.particleSize = parseNumberAttribute(particles, 'size', parameters.particleSize);
    parameters.particleSizeVelocity = parseNumberAttribute(particles, 'sizeVelocity', parameters.particleSizeVelocity);
    parameters.gravity = parseUnifiedVector(findXmlChild(particles, 'gravity'), parameters.gravity);
  }

  if (colorFrames) {
    parameters.colorKeyFrameBits = parseIntegerAttribute(colorFrames, 'keyFrameBits', parameters.colorKeyFrameBits);
    const frames = (colorFrames.childrenList || []).filter((child) => child.tagName === 'frame');
    for (const frame of frames) {
      const index = parseIntegerAttribute(frame, 'index', -1);
      if (index < 0 || index >= PSP_NUM_COLOR_KEYS) {
        continue;
      }

      parameters.colorFrames[index] = {
        r: parseNumberAttribute(frame, 'r', parameters.colorFrames[index].r),
        g: parseNumberAttribute(frame, 'g', parameters.colorFrames[index].g),
        b: parseNumberAttribute(frame, 'b', parameters.colorFrames[index].b),
        a: parseNumberAttribute(frame, 'a', parameters.colorFrames[index].a),
      };
    }
  }

  return parameters;
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

    if (!(p.pspFlags & PSP_F_RELATIVE_TRANSFORM)) {
      position.set(0, 0, 0);
    }

    if (p.particlePositionRandomizer) {
      position.x += fRand() * p.particlePositionRandomizer;
      position.y += fRand() * p.particlePositionRandomizer;
      position.z += fRand() * p.particlePositionRandomizer;
    }

    if (p.pspFlags & PSP_F_RELATIVE_VELOCITY) {
      // The demo emitter is stationary, so inherited velocity is zero.
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

  dispose() {
    this.geometry.dispose();
    this.material.dispose();
    this.texture?.dispose();
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
  const degenerateDamp = damp.x * damp.x + damp.y * damp.y + damp.z * damp.z === 0;
  const direction = new THREE.Vector3(parameters.emitterDirection.x, parameters.emitterDirection.y, parameters.emitterDirection.z);

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
      pixelRatio: { value: Math.min(window.devicePixelRatio, 2) },
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

  applyBlendMode(material, parameters);
  return material;
}

function applyBlendMode(material, parameters) {
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

function frameParticleSystem() {
  if (!activeSystem) {
    return;
  }

  const radius = Math.max(50, activeSystem.parameters.boundingSphereRadius || activeSystem.parameters.particleSize * 8 || 500);
  cameraTarget.set(0, 0, 0);
  orbit.distance = radius * 2.2;
  orbit.yaw = Math.PI / 4;
  orbit.pitch = Math.PI / 6;
  camera.near = Math.max(0.01, orbit.distance / 1000);
  camera.far = Math.max(1000, orbit.distance * 100);
  camera.updateProjectionMatrix();
  grid.scale.setScalar(Math.max(1, radius / 500));
  axes.scale.setScalar(Math.max(1, radius / 150));
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
      orbit.yaw -= dx * 0.006;
      orbit.pitch = clamp(orbit.pitch - dy * 0.006, -Math.PI / 2 + 0.05, Math.PI / 2 - 0.05);
      updateCamera();
    }
  });

  target.addEventListener('pointerup', (event) => {
    dragging = false;
    target.releasePointerCapture(event.pointerId);
  });

  target.addEventListener('pointercancel', () => {
    dragging = false;
  });

  target.addEventListener('contextmenu', (event) => event.preventDefault());

  target.addEventListener('wheel', (event) => {
    event.preventDefault();
    orbit.distance = Math.max(0.1, orbit.distance * Math.exp(event.deltaY * 0.001));
    updateCamera();
  }, { passive: false });
}

function panCamera(dx, dy) {
  const offset = new THREE.Vector3().subVectors(camera.position, cameraTarget);
  const right = new THREE.Vector3().crossVectors(camera.up, offset).normalize();
  const up = new THREE.Vector3().crossVectors(offset, right).normalize();
  const scale = orbit.distance / Math.max(canvas.clientHeight, 1);

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
  const width = canvas.clientWidth || 1;
  const height = canvas.clientHeight || 1;
  renderer.setSize(width, height, false);
  camera.aspect = width / Math.max(height, 1);
  camera.updateProjectionMatrix();
}

function animate(now = performance.now()) {
  requestAnimationFrame(animate);
  const dt = Math.min(0.05, Math.max(0, (now - lastTime) / 1000));
  lastTime = now;

  activeSystem?.update(dt);
  renderer.render(scene, camera);
}

function updateStats(displayName, parameters, textureCanvas) {
  const textureText = textureCanvas
    ? `${textureCanvas.dataset.textureName} (${textureCanvas.width}x${textureCanvas.height})`
    : 'fallback sprite';
  statsLine.textContent = `${displayName} | ${activeSystem.maxParticles} particle slots | ${parameters.textureName || 'no texture'} -> ${textureText}`;
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

function isUnifiedParticleRoot(rootNode) {
  return rootNode?.tagName === 'particleEditor' && rootNode.attributes?.format === 'cfw-unified-particle';
}

function findXmlChild(node, tagName) {
  return (node?.childrenList || []).find((child) => child.tagName === tagName) || null;
}

function attr(node, name, fallback = '') {
  return node?.attributes?.[name] ?? fallback;
}

function parseNumberAttribute(node, name, fallback = 0) {
  const value = Number(attr(node, name, ''));
  return Number.isFinite(value) ? value : fallback;
}

function parseIntegerAttribute(node, name, fallback = 0) {
  const raw = attr(node, name, '');
  if (!raw) {
    return fallback;
  }

  const value = raw.startsWith('0x') || raw.startsWith('0X')
    ? Number.parseInt(raw, 16)
    : Number.parseInt(raw, 10);
  return Number.isFinite(value) ? value : fallback;
}

function parseUnifiedVector(node, fallback) {
  if (!node) {
    return fallback;
  }

  return {
    x: parseNumberAttribute(node, 'x', fallback.x),
    y: parseNumberAttribute(node, 'y', fallback.y),
    z: parseNumberAttribute(node, 'z', fallback.z),
  };
}

function readVector3(view, offset) {
  return {
    x: view.getFloat32(offset, true),
    y: view.getFloat32(offset + 4, true),
    z: view.getFloat32(offset + 8, true),
  };
}

function readCString(bytes, offset, maxLength) {
  let end = offset;
  const limit = offset + maxLength;
  while (end < limit && bytes[end] !== 0) {
    end += 1;
  }
  return new TextDecoder('ascii').decode(bytes.slice(offset, end));
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

function vectorLengthSq(vector) {
  return vector.x * vector.x + vector.y * vector.y + vector.z * vector.z;
}
