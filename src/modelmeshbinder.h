#ifndef MODEL_MESH_BINDER_H
#define MODEL_MESH_BINDER_H
#include <QOpenGLVertexArrayObject>
#include <QMutex>
#include <QOpenGLBuffer>
#include <QString>
#include "mesh.h"

class ModelMeshBinder
{
public:
    ModelMeshBinder();
    ~ModelMeshBinder();
    void updateMesh(Mesh *mesh);
    void exportMeshAsObj(const QString &filename);
    void initialize();
    void paint();
    void cleanup();
    void showWireframes();
    void hideWireframes();
    bool isWireframesVisible();
private:
    Mesh *m_mesh;
    int m_renderTriangleVertexCount;
    int m_renderEdgeVertexCount;
    bool m_meshUpdated;
    bool m_showWireframes;
private:
    QOpenGLVertexArrayObject m_vaoTriangle;
    QOpenGLBuffer m_vboTriangle;
    QOpenGLVertexArrayObject m_vaoEdge;
    QOpenGLBuffer m_vboEdge;
    QMutex m_meshMutex;
};

#endif
