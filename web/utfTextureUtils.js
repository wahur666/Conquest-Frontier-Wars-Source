export function loadTextureCanvases(root) {
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

export function findTextureCanvas(textureCanvases, textureName) {
  if (textureCanvases.has(textureName)) {
    return textureCanvases.get(textureName);
  }

  const baseName = textureName.split('.')[0].toLowerCase();
  const match = Array.from(textureCanvases.keys()).find((key) => key.split('.')[0].toLowerCase() === baseName);
  return match ? textureCanvases.get(match) : null;
}

export function getInt32(node) {
  if (!node?.value || node.value.byteLength < 4) {
    return null;
  }

  return new DataView(node.value.buffer, node.value.byteOffset, node.value.byteLength).getInt32(0, true);
}

export function findChildCI(node, name) {
  if (!node?.children) {
    return null;
  }

  const lower = name.toLowerCase();
  return Object.values(node.children).find((child) => child.name.toLowerCase() === lower) || null;
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
