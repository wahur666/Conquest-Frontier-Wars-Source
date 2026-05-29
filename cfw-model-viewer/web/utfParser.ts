// parser/utfParser.ts
export type UtfNode = {
    name: string
    dwAttributes: number
    value?: Uint8Array
    width?: number
    height?: number
    children?: Record<string, UtfNode>
}

function getInt(view: DataView, pos: number): [number, number] {
    return [view.getInt32(pos, true), pos + 4]
}

function readCString(u8: Uint8Array, start: number): string {
    let end = start
    while (end < u8.length && u8[end] !== 0) end++
    return new TextDecoder("ascii").decode(u8.slice(start, end))
}

export async function loadUtfFile(file: File): Promise<Record<string, UtfNode>> {
    const ab = await file.arrayBuffer()
    const buf = new Uint8Array(ab)
    const view = new DataView(ab)

    let pos = 0, sig, ver
    ;[sig, pos] = getInt(view, pos)
    ;[ver, pos] = getInt(view, pos)

    if (sig !== 0x20465455 || ver !== 0x101) throw new Error("Unsupported UTF")

    // node block info
    let nodeBlockOffset, nodeSize
    ;[nodeBlockOffset, pos] = getInt(view, pos)
    ;[nodeSize, pos] = getInt(view, pos)

    // unknown / header
    pos += 8

    // string block info
    let stringBlockOffset, stringBlockSize
    ;[stringBlockOffset, pos] = getInt(view, pos)
    ;[stringBlockSize, pos] = getInt(view, pos)

    pos += 4 // padding

    // data block offset
    let dataBlockOffset
    ;[dataBlockOffset, pos] = getInt(view, pos)

    const root: Record<string, UtfNode> = {}
    parseNode(buf, view, nodeBlockOffset, 0, stringBlockOffset, dataBlockOffset, root)
    return root
}

function parseNode(
    buf: Uint8Array,
    view: DataView,
    nodeBlockStart: number,
    nodeStart: number,
    stringBlockOffset: number,
    dataBlockOffset: number,
    parent: Record<string, UtfNode>
) {
    let offset = nodeBlockStart + nodeStart

    while (true) {
        // read node fields
        let dwNext, dwName, dwAttributes, sharing
        let dwDataOffset, dwSpaceAllocated, dwSpaceUsed, dwUncompressedSize

        ;[dwNext, offset] = getInt(view, offset)
        ;[dwName, offset] = getInt(view, offset)
        ;[dwAttributes, offset] = getInt(view, offset)
        ;[sharing, offset] = getInt(view, offset)
        ;[dwDataOffset, offset] = getInt(view, offset)
        ;[dwSpaceAllocated, offset] = getInt(view, offset)
        ;[dwSpaceUsed, offset] = getInt(view, offset)
        ;[dwUncompressedSize, offset] = getInt(view, offset)

        offset += 12 // skip DOS times

        const name = readCString(buf, stringBlockOffset + dwName)

        const node: UtfNode = { name, dwAttributes }

        // if leaf node, extract value
        const isLeaf = (dwAttributes & 0x80) !== 0
        if (isLeaf && dwSpaceUsed > 0) {
            const t = dwDataOffset + dataBlockOffset
            node.value = buf.slice(t, t + dwSpaceUsed)
        }

        // attach to parent
        parent[name] = node

        // recurse if folder node
        if (!isLeaf && dwDataOffset > 0) {
            node.children = {}
            parseNode(buf, view, nodeBlockStart, dwDataOffset, stringBlockOffset, dataBlockOffset, node.children)
        }

        // optional: extract width/height for image leafs
        if (isLeaf && node.children) {
            const widthNode = node.children["Image X size"]
            const heightNode = node.children["Image Y size"]
            if (widthNode && widthNode.value) {
                node.width = new DataView(widthNode.value.buffer).getInt32(0, true)
            }
            if (heightNode && heightNode.value) {
                node.height = new DataView(heightNode.value.buffer).getInt32(0, true)
            }
        }

        if (dwNext === 0) break
        offset = nodeBlockStart + dwNext
    }
}