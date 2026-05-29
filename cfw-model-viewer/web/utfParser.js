export function loadUtfFile(file) {
  return file.arrayBuffer().then((ab) => {
    const firstBytes = new Uint8Array(ab, 0, Math.min(64, ab.byteLength));
    const firstText = new TextDecoder('ascii').decode(firstBytes).trimStart();

    if (firstText.startsWith('<')) {
      return parseUtfXmlString(new TextDecoder('utf-8').decode(ab));
    }

    return parseUtfArrayBuffer(ab);
  });
}

export function parseUtfArrayBuffer(ab) {
  const buf = new Uint8Array(ab);
  const view = new DataView(ab);

  let pos = 0;
  let sig;
  let ver;
  [sig, pos] = getInt(view, pos);
  [ver, pos] = getInt(view, pos);

  if (sig !== 0x20465455 || ver !== 0x101) {
    throw new Error('Unsupported UTF file. Expected signature 0x20465455 and version 0x101.');
  }

  let nodeBlockOffset;
  [nodeBlockOffset, pos] = getInt(view, pos);
  pos += 4;
  pos += 8;

  let stringBlockOffset;
  [stringBlockOffset, pos] = getInt(view, pos);
  pos += 4;
  pos += 4;

  let dataBlockOffset;
  [dataBlockOffset, pos] = getInt(view, pos);

  const root = {};
  parseNode(buf, view, nodeBlockOffset, 0, stringBlockOffset, dataBlockOffset, root);
  return root;
}

function parseNode(buf, view, nodeBlockStart, nodeStart, stringBlockOffset, dataBlockOffset, parent) {
  let offset = nodeBlockStart + nodeStart;

  while (true) {
    let dwNext;
    let dwName;
    let dwAttributes;
    let dwDataOffset;
    let dwSpaceUsed;

    [dwNext, offset] = getInt(view, offset);
    [dwName, offset] = getInt(view, offset);
    [dwAttributes, offset] = getInt(view, offset);
    [, offset] = getInt(view, offset);
    [dwDataOffset, offset] = getInt(view, offset);
    [, offset] = getInt(view, offset);
    [dwSpaceUsed, offset] = getInt(view, offset);
    [, offset] = getInt(view, offset);

    offset += 12;

    const name = readCString(buf, stringBlockOffset + dwName);
    const node = { name, dwAttributes };
    const isLeaf = (dwAttributes & 0x80) !== 0;

    if (isLeaf && dwSpaceUsed > 0) {
      const start = dataBlockOffset + dwDataOffset;
      node.value = buf.slice(start, start + dwSpaceUsed);
    }

    parent[name] = node;

    if (!isLeaf && dwDataOffset > 0) {
      node.children = {};
      parseNode(buf, view, nodeBlockStart, dwDataOffset, stringBlockOffset, dataBlockOffset, node.children);
    }

    if (dwNext === 0) {
      break;
    }

    offset = nodeBlockStart + dwNext;
  }
}

function getInt(view, pos) {
  return [view.getInt32(pos, true), pos + 4];
}

function readCString(u8, start) {
  let end = start;
  while (end < u8.length && u8[end] !== 0) {
    end += 1;
  }

  return new TextDecoder('ascii').decode(u8.slice(start, end));
}

export function parseUtfXmlString(xmlText) {
  const documentXml = new DOMParser().parseFromString(xmlText, 'application/xml');
  const parserError = documentXml.querySelector('parsererror');

  if (parserError) {
    throw new Error(parserError.textContent || 'Unable to parse UTF XML.');
  }

  const rootElement = documentXml.documentElement;
  const rootNode = elementToNode(rootElement);
  return { '\\': rootNode };
}

function elementToNode(element) {
  const name = element.getAttribute('name') || element.tagName || '\\';
  const node = {
    name,
    tagName: element.tagName,
    attributes: Object.fromEntries(Array.from(element.attributes, (attribute) => [attribute.name, attribute.value])),
    dwAttributes: element.tagName === 'file' ? 0x80 : 0,
  };

  if (element.tagName === 'file') {
    const text = (element.textContent || '').replace(/\s+/g, '');
    node.value = base64ToUint8Array(text);
    return node;
  }

  node.children = {};
  node.childrenList = [];
  for (const child of element.children) {
    const childNode = elementToNode(child);
    node.children[childNode.name] = childNode;
    node.childrenList.push(childNode);
  }

  return node;
}

function base64ToUint8Array(base64) {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);

  for (let i = 0; i < binary.length; i += 1) {
    bytes[i] = binary.charCodeAt(i);
  }

  return bytes;
}
