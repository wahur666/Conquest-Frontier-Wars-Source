import { useEffect, useRef } from "react"
import * as THREE from "three"

interface UtfNode {
    name: string
    dwAttributes: number
    value?: Uint8Array
    width?: number
    height?: number
    children?: Record<string, UtfNode>
}

interface MeshViewerProps {
    node: UtfNode | Record<string, UtfNode>
}

/** Extract float array from raw buffer */
function getFloatArray(node: UtfNode | undefined): Float32Array | null {
    if (!node?.value || !(node.value instanceof Uint8Array)) {
        return null
    }

    return new Float32Array(node.value.buffer, node.value.byteOffset, node.value.byteLength / 4)
}

/** Extract int array from raw buffer */
function getIntArray(node: UtfNode | undefined): Uint32Array | null {
    if (!node?.value || !(node.value instanceof Uint8Array)) {
        return null
    }

    return new Uint32Array(node.value.buffer, node.value.byteOffset, node.value.byteLength / 4)
}

/** Extract single int from buffer */
function getInt32(node: UtfNode | undefined): number | null {
    if (!node?.value || !(node.value instanceof Uint8Array)) {
        return null
    }

    const dv = new DataView(node.value.buffer, node.value.byteOffset)
    return dv.getInt32(0, true)
}

/** Extract RGB values (3 bytes) */
function getRGB(node: UtfNode | undefined): { r: number; g: number; b: number } | null {
    if (!node?.value || !(node.value instanceof Uint8Array)) {
        return null
    }

    // Check if it's bytes (0-255) or floats (0-1)
    if (node.value.byteLength >= 12) {
        // Probably floats (4 bytes each)
        const dv = new DataView(node.value.buffer, node.value.byteOffset)
        return {
            r: dv.getFloat32(0, true),
            g: dv.getFloat32(4, true),
            b: dv.getFloat32(8, true)
        }
    } else if (node.value.byteLength >= 3) {
        // Probably bytes (1 byte each)
        return {
            r: node.value[0] / 255,
            g: node.value[1] / 255,
            b: node.value[2] / 255
        }
    }

    return null
}

/** Extract string from buffer */
function getString(node: UtfNode | undefined): string | null {
    if (!node?.value || !(node.value instanceof Uint8Array)) {
        return null
    }

    const decoder = new TextDecoder("ascii")
    let end = 0
    while (end < node.value.length && node.value[end] !== 0) end++
    return decoder.decode(node.value.slice(0, end))
}

/** Load material and its properties */
function loadMaterial(
    materialNode: UtfNode,
    textureLibrary: Record<string, UtfNode>,
    textureCanvases: Map<string, HTMLCanvasElement>
): THREE.MeshPhongMaterial {
    const materialName = materialNode.name
    const diffuseNode = materialNode.children?.["Diffuse"]
    let texture: THREE.Texture | null = null
    let diffuseColor = new THREE.Color(0xffffff)

    // Extract all color properties
    let ambientColor = { r: 1, g: 1, b: 1 }
    let specularColor = { r: 0.1, g: 0.1, b: 0.1 }
    let diffuseColorVec = { r: 1, g: 1, b: 1 }
    let shininess = 30
    let textureName = "NONE"

    // Read Ambient
    const ambientNode = materialNode.children?.["Ambient"]
    const ambientConstantNode = ambientNode?.children?.["Constant"]
    if (ambientConstantNode) {
        const rgb = getRGB(ambientConstantNode)
        if (rgb) {
            ambientColor = rgb
        }
    }

    // Read Diffuse color
    const diffuseConstantNode = diffuseNode?.children?.["Constant"]
    if (diffuseConstantNode) {
        const rgb = getRGB(diffuseConstantNode)
        if (rgb) {
            diffuseColorVec = rgb
            diffuseColor.setRGB(rgb.r, rgb.g, rgb.b)
        }
    }

    // Read Diffuse map texture
    const mapNode = diffuseNode?.children?.["Map"]
    if (mapNode?.children) {
        const nameNode = mapNode.children["Name"]
        const texName = getString(nameNode)

        if (texName) {
            textureName = texName

            // Try exact match first
            if (textureCanvases.has(texName)) {
                const canvas = textureCanvases.get(texName)!
                const canvasTexture = new THREE.CanvasTexture(canvas)
                canvasTexture.magFilter = THREE.NearestFilter
                canvasTexture.minFilter = THREE.NearestFilter
                texture = canvasTexture
            } else {
                // Try partial match
                const baseName = texName.split('.')[0].toLowerCase()
                const match = Array.from(textureCanvases.keys()).find(
                    key => key.split('.')[0].toLowerCase() === baseName
                )
                if (match) {
                    const canvas = textureCanvases.get(match)!
                    const canvasTexture = new THREE.CanvasTexture(canvas)
                    canvasTexture.magFilter = THREE.NearestFilter
                    canvasTexture.minFilter = THREE.NearestFilter
                    texture = canvasTexture
                }
            }
        }
    }

    // Read Specular
    const specularNode = materialNode.children?.["Specular"]
    const specularConstantNode = specularNode?.children?.["Constant"]
    if (specularConstantNode) {
        const rgb = getRGB(specularConstantNode)
        if (rgb) {
            specularColor = rgb
        }
    }

    // Read Shininess
    const shininessNode = materialNode.children?.["Shininess"]?.children?.["Constant"]
    let shininessRaw = 0
    if (shininessNode?.value && shininessNode.value.byteLength >= 4) {
        const dv = new DataView(shininessNode.value.buffer, shininessNode.value.byteOffset)
        shininessRaw = dv.getFloat32(0, true)
        shininess = shininessRaw * 2
    } else if (shininessNode?.value && shininessNode.value.byteLength >= 1) {
        shininessRaw = shininessNode.value[0]
        shininess = shininessRaw * 2
    }

    // DEBUG LOG
    console.log(`=== MATERIAL: ${materialName} ===`)
    console.log(`  Ambient:    (${ambientColor.r.toFixed(3)}, ${ambientColor.g.toFixed(3)}, ${ambientColor.b.toFixed(3)})`)
    console.log(`  Diffuse:    (${diffuseColorVec.r.toFixed(3)}, ${diffuseColorVec.g.toFixed(3)}, ${diffuseColorVec.b.toFixed(3)})`)
    console.log(`  Specular:   (${specularColor.r.toFixed(3)}, ${specularColor.g.toFixed(3)}, ${specularColor.b.toFixed(3)})`)
    console.log(`  Shininess:  ${shininessRaw.toFixed(3)} (scaled: ${shininess.toFixed(1)})`)
    console.log(`  Texture:    ${textureName} ${texture ? "✓ LOADED" : "✗ NOT FOUND"}`)
    console.log("")

    return new THREE.MeshPhongMaterial({
        color: diffuseColor,
        specular: new THREE.Color().setRGB(specularColor.r, specularColor.g, specularColor.b),
        shininess: Math.max(1, shininess),
        map: texture,
        side: THREE.DoubleSide,
        wireframe: false
    })
}

export function MeshViewer({ node }: MeshViewerProps) {
    const canvasRef = useRef<HTMLCanvasElement>(null)
    const sceneRef = useRef<THREE.Scene | null>(null)

    useEffect(() => {
        if (!canvasRef.current) return

        try {
            let rootNode: UtfNode | undefined

            if ("children" in node && node.children) {
                rootNode = node as UtfNode
            } else if ("\\" in node) {
                rootNode = (node as Record<string, UtfNode>)["\\"]
            }

            if (!rootNode?.children) return

            const meshNode = rootNode.children["openFLAME 3D N-mesh"]
            if (!meshNode?.children) return

            const root = meshNode.children

            // ===== TEXTURES =====
            const textureLibrary = root["Texture library"]?.children || {}
            const textureCanvases = new Map<string, HTMLCanvasElement>()

            for (const [texName, texNode] of Object.entries(textureLibrary)) {
                if (!texNode.children) continue

                let pal = texNode.children["Palette 8 bit"]
                if (pal?.children) {
                    const palette = pal.children["Palette RGB 888"]?.value
                    const indices = pal.children["MIP0"]?.children?.["Image indices"]?.value
                    const w = getInt32(texNode.children["Image X size"])
                    const h = getInt32(texNode.children["Image Y size"])

                    if (palette && indices && w && h) {
                        const canvas = document.createElement("canvas")
                        canvas.width = w
                        canvas.height = h
                        const ctx = canvas.getContext("2d")!
                        const rgba = palette8ToRGBA(indices, palette, w, h)
                        ctx.putImageData(new ImageData(rgba, w, h), 0, 0)
                        textureCanvases.set(texName, canvas)
                        continue
                    }
                }

                pal = texNode.children["Format_TRUE_3__8_8_8"]
                if (pal?.children) {
                    const palette = pal.children["Format_TRUE_3__8_8_8"]?.value
                    const indices = pal.children["MIP0"]?.children?.["Image indices"]?.value
                    const w = getInt32(texNode.children["Image X size"])
                    const h = getInt32(texNode.children["Image Y size"])

                    if (palette && indices && w && h) {
                        const canvas = document.createElement("canvas")
                        canvas.width = w
                        canvas.height = h
                        const ctx = canvas.getContext("2d")!
                        const rgba = palette8ToRGBA(indices, palette, w, h)
                        ctx.putImageData(new ImageData(rgba, w, h), 0, 0)
                        textureCanvases.set(texName, canvas)
                        continue
                    }
                }

                const rgb = texNode.children["True RGB 565"]
                if (rgb?.children && !textureCanvases.has(texName)) {
                    const colors = rgb.children["MIP0"]?.children?.["Image colors"]?.value
                    const alpha = rgb.children["MIP0"]?.children?.["Image Alpha 8 bit"]?.value
                    const w = getInt32(texNode.children["Image X size"])
                    const h = getInt32(texNode.children["Image Y size"])

                    if (colors && w && h) {
                        const canvas = document.createElement("canvas")
                        canvas.width = w
                        canvas.height = h
                        const ctx = canvas.getContext("2d")!
                        const rgba = rgb565WithAlphaToRGBA(colors, alpha, w, h)
                        ctx.putImageData(new ImageData(rgba, w, h), 0, 0)
                        textureCanvases.set(texName, canvas)
                    }
                }
            }

            // ===== MATERIALS =====
            const materialsNode = root["Material library"]?.children || {}
            const materials: THREE.MeshPhongMaterial[] = []
            const materialIdMap = new Map<number, number>()

            const matKeys = Object.keys(materialsNode).filter(
                k => k !== "Material count" && k !== "name" && k !== "value"
            )

            matKeys.forEach((key, idx) => {
                const matNode = materialsNode[key]
                const idNode = matNode.children?.["Material identifier"]
                const id = idNode?.value
                    ? new DataView(idNode.value.buffer).getInt32(0, true)
                    : idx

                materials.push(loadMaterial(matNode, textureLibrary, textureCanvases))
                materialIdMap.set(id, idx)
            })

            // ===== RAW DATA =====
            const allVertices = getFloatArray(root["Vertices"]?.children?.["Object vertex list"])
            const vertexBatchList = getIntArray(root["Vertices"]?.children?.["Vertex batch list"])
            const textureBatchList = getIntArray(root["Vertices"]?.children?.["Texture batch list"])
            const allUvs = getFloatArray(root["Vertices"]?.children?.["Texture vertex list"])
            const normals = getFloatArray(root["Normals"]?.children?.["Surface normal list"])

            if (!allVertices) return

            // ===== GEOMETRY (FIXED UVs) =====
            const faceGroups = root["Face groups"]?.children
            if (!faceGroups) return

            const vertexMap = new Map<string, number>()
            const positions: number[] = []
            const uvs: number[] = []
            const indices: number[] = []
            const groups: Array<{ start: number; count: number; materialIndex: number }> = []

            let cursor = 0

            for (const key of Object.keys(faceGroups).filter(k => k.startsWith("Group"))) {
                const g = faceGroups[key].children
                if (!g) continue

                const matID = g["Material"]?.value
                    ? new DataView(g["Material"].value.buffer).getInt32(0, true)
                    : 0

                const matIndex = materialIdMap.get(matID) ?? 0
                const chain = getIntArray(g["Face vertex chain"])
                console.log("Chain length", chain.length)
                if (!chain) continue

                const start = indices.length
                let count = 0

                for (let i = 0; i < chain.length; i++) {
                    const batch = chain[i]
                    const vIdx = vertexBatchList ? vertexBatchList[batch] : batch
                    const uvIdx = textureBatchList ? textureBatchList[batch] : -1

                    const key = `${vIdx}_${uvIdx}`
                    let idx: number

                    if (vertexMap.has(key)) {
                        idx = vertexMap.get(key)!
                    } else {
                        idx = cursor++
                        vertexMap.set(key, idx)

                        positions.push(
                            allVertices[vIdx * 3] / 100,
                            allVertices[vIdx * 3 + 1] / 100,
                            allVertices[vIdx * 3 + 2] / 100
                        )

                        if (allUvs && uvIdx >= 0) {
                            uvs.push(allUvs[uvIdx * 2], 1 - allUvs[uvIdx * 2 + 1])
                            // uvs.push(allUvs[uvIdx * 2], allUvs[uvIdx * 2 + 1])
                        } else {
                            uvs.push(0, 0)
                        }
                    }

                    indices.push(idx)
                    count++
                }

                groups.push({ start, count, materialIndex: matIndex })
            }

            // ===== THREE SETUP =====
            const canvas = canvasRef.current
            const scene = new THREE.Scene()
            scene.background = new THREE.Color(0x1a1a1a)

            const camera = new THREE.PerspectiveCamera(75, canvas.clientWidth / canvas.clientHeight, 0.1, 10000)
            const renderer = new THREE.WebGLRenderer({ canvas, antialias: true })
            renderer.setSize(canvas.clientWidth, canvas.clientHeight)

            const geometry = new THREE.BufferGeometry()
            geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3))
            geometry.setAttribute("uv", new THREE.Float32BufferAttribute(uvs, 2))
            geometry.setIndex(indices)

            if (normals && normals.length >= positions.length) {
                geometry.setAttribute("normal", new THREE.BufferAttribute(normals, 3))
            } else {
                geometry.computeVertexNormals()
            }

            groups.forEach(g => geometry.addGroup(g.start, g.count, g.materialIndex))

            const mesh = new THREE.Mesh(geometry, materials)
            scene.add(mesh)

            scene.add(new THREE.AmbientLight(0xffffff, 0.6))
            const light = new THREE.DirectionalLight(0xffffff, 0.8)
            light.position.set(5, 10, 7)
            scene.add(light)

            const box = new THREE.Box3().setFromObject(mesh)
            const center = box.getCenter(new THREE.Vector3())
            const size = box.getSize(new THREE.Vector3())
            camera.position.set(center.x, center.y, center.z + Math.max(size.x, size.y, size.z))
            camera.lookAt(center)

            // ===== CAMERA CONTROLS =====
            let isDragging = false
            let prev = { x: 0, y: 0 }
            const rotation = { x: 0, y: 0 }

            const onMouseDown = (e: MouseEvent) => {
                isDragging = true
                prev = { x: e.clientX, y: e.clientY }
            }

            const onMouseMove = (e: MouseEvent) => {
                if (!isDragging) return

                const dx = e.clientX - prev.x
                const dy = e.clientY - prev.y

                rotation.y += dx * 0.01
                rotation.x += dy * 0.01

                mesh.rotation.set(rotation.x, rotation.y, 0)
                prev = { x: e.clientX, y: e.clientY }
            }

            const onMouseUp = () => {
                isDragging = false
            }

            const onWheel = (e: WheelEvent) => {
                e.preventDefault()
                camera.position.z += e.deltaY * 0.1
            }

// attach
            canvas.addEventListener("mousedown", onMouseDown)
            canvas.addEventListener("mousemove", onMouseMove)
            canvas.addEventListener("mouseup", onMouseUp)
            canvas.addEventListener("wheel", onWheel, { passive: false })

            const animate = () => {
                requestAnimationFrame(animate)
                renderer.render(scene, camera)
            }
            animate()
            const onResize = () => {
                const w = canvas.clientWidth
                const h = canvas.clientHeight
                camera.aspect = w / h
                camera.updateProjectionMatrix()
                renderer.setSize(w, h)
            }

            window.addEventListener("resize", onResize)
            return () => {
                geometry.dispose()
                materials.forEach(m => m.dispose())
                renderer.dispose()
                canvas.removeEventListener("mousedown", onMouseDown)
                canvas.removeEventListener("mousemove", onMouseMove)
                canvas.removeEventListener("mouseup", onMouseUp)
                canvas.removeEventListener("wheel", onWheel)
                window.removeEventListener("resize", onResize)
            }
        } catch (e) {
            console.error(e)
        }
    }, [node])

    return (
        <div className="w-full h-96 border border-gray-400 rounded">
            <canvas ref={canvasRef} className="w-full h-full" />
        </div>
    )
}

/** Palette8 + indices → RGBA8888 */
function palette8ToRGBA(
    indices: Uint8Array,
    palette: Uint8Array,
    width: number,
    height: number
): Uint8ClampedArray {
    const pixelCount = Math.min(indices.byteLength, width * height)
    const out = new Uint8ClampedArray(pixelCount * 4)

    for (let i = 0; i < pixelCount; i++) {
        const idx = indices[i]
        const paletteIdx = idx * 3
        out[i * 4] = palette[paletteIdx] || 0
        out[i * 4 + 1] = palette[paletteIdx + 1] || 0
        out[i * 4 + 2] = palette[paletteIdx + 2] || 0
        out[i * 4 + 3] = 255
    }
    return out
}

/** RGB565 → RGBA8888 with optional alpha channel */
function rgb565WithAlphaToRGBA(
    colors: Uint8Array,
    alpha: Uint8Array | undefined,
    width: number,
    height: number
): Uint8ClampedArray {
    const dv = new DataView(colors.buffer, colors.byteOffset, colors.byteLength)
    const pixelCount = Math.floor(dv.byteLength / 2)
    const out = new Uint8ClampedArray(pixelCount * 4)

    for (let i = 0; i < pixelCount; i++) {
        const val = dv.getUint16(i * 2, true)
        out[i * 4] = ((val >> 11) & 0x1f) << 3
        out[i * 4 + 1] = ((val >> 5) & 0x3f) << 2
        out[i * 4 + 2] = (val & 0x1f) << 3
        out[i * 4 + 3] = alpha && i < alpha.byteLength ? alpha[i] : 255
    }
    return out
}