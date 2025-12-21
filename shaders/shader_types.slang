module shader_types;

public struct MeshletTriangleIndex {
    public uint8_t triangleIndex;
}

public struct MeshletTriangleData {
    public MeshletTriangleIndex[] meshletTriangles;
};