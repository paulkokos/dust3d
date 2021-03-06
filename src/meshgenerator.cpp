#include <vector>
#include <QGuiApplication>
#include "meshgenerator.h"
#include "util.h"
#include "skeletondocument.h"
#include "meshlite.h"
#include "modelofflinerender.h"
#include "meshutil.h"
#include "theme.h"

bool MeshGenerator::enableDebug = false;

MeshGenerator::MeshGenerator(SkeletonSnapshot *snapshot, QThread *thread) :
    m_snapshot(snapshot),
    m_mesh(nullptr),
    m_preview(nullptr),
    m_requirePreview(false),
    m_previewRender(nullptr),
    m_thread(thread)
{
}

MeshGenerator::~MeshGenerator()
{
    delete m_snapshot;
    delete m_mesh;
    delete m_preview;
    for (const auto &partPreviewIt: m_partPreviewMap) {
        delete partPreviewIt.second;
    }
    for (const auto &render: m_partPreviewRenderMap) {
        delete render.second;
    }
    delete m_previewRender;
}

void MeshGenerator::addPreviewRequirement()
{
    m_requirePreview = true;
    if (nullptr == m_previewRender) {
        m_previewRender = new ModelOfflineRender;
        m_previewRender->setRenderThread(m_thread);
    }
}

void MeshGenerator::addPartPreviewRequirement(const QString &partId)
{
    //qDebug() << "addPartPreviewRequirement:" << partId;
    m_requirePartPreviewMap.insert(partId);
    if (m_partPreviewRenderMap.find(partId) == m_partPreviewRenderMap.end()) {
        ModelOfflineRender *render = new ModelOfflineRender;
        render->setRenderThread(m_thread);
        m_partPreviewRenderMap[partId] = render;
    }
}

Mesh *MeshGenerator::takeResultMesh()
{
    Mesh *resultMesh = m_mesh;
    m_mesh = nullptr;
    return resultMesh;
}

QImage *MeshGenerator::takePreview()
{
    QImage *resultPreview = m_preview;
    m_preview = nullptr;
    return resultPreview;
}

QImage *MeshGenerator::takePartPreview(const QString &partId)
{
    QImage *resultImage = m_partPreviewMap[partId];
    m_partPreviewMap[partId] = nullptr;
    return resultImage;
}

void MeshGenerator::resolveBoundingBox(QRectF *mainProfile, QRectF *sideProfile, const QString &partId)
{
    m_snapshot->resolveBoundingBox(mainProfile, sideProfile, partId);
}

void MeshGenerator::process()
{
    if (nullptr == m_snapshot)
        return;
    
    void *meshliteContext = meshlite_create_context();
    std::map<QString, int> partBmeshMap;
    std::map<QString, int> bmeshNodeMap;
    
    QRectF mainProfile, sideProfile;
    resolveBoundingBox(&mainProfile, &sideProfile);
    float longHeight = mainProfile.height();
    if (mainProfile.width() > longHeight)
        longHeight = mainProfile.width();
    if (sideProfile.width() > longHeight)
        longHeight = sideProfile.width();
    float mainProfileMiddleX = mainProfile.x() + mainProfile.width() / 2;
    float sideProfileMiddleX = sideProfile.x() + sideProfile.width() / 2;
    float mainProfileMiddleY = mainProfile.y() + mainProfile.height() / 2;
    float originX = valueOfKeyInMapOrEmpty(m_snapshot->canvas, "originX").toFloat();
    float originY = valueOfKeyInMapOrEmpty(m_snapshot->canvas, "originY").toFloat();
    float originZ = valueOfKeyInMapOrEmpty(m_snapshot->canvas, "originZ").toFloat();
    bool originSettled = false;
    if (originX > 0 && originY > 0 && originZ > 0) {
        //qDebug() << "Use settled origin: " << originX << originY << originZ << " calculated:" << mainProfileMiddleX << mainProfileMiddleY << sideProfileMiddleX;
        mainProfileMiddleX = originX;
        mainProfileMiddleY = originY;
        sideProfileMiddleX = originZ;
        originSettled = true;
    } else {
        //qDebug() << "No settled origin, calculated:" << mainProfileMiddleX << mainProfileMiddleY << sideProfileMiddleX;
    }
    
    for (const auto &partIdIt: m_snapshot->partIdList) {
        const auto &part = m_snapshot->parts.find(partIdIt);
        if (part == m_snapshot->parts.end())
            continue;
        QString disabledString = valueOfKeyInMapOrEmpty(part->second, "disabled");
        bool isDisabled = isTrueValueString(disabledString);
        if (isDisabled)
            continue;
        bool subdived = isTrueValueString(valueOfKeyInMapOrEmpty(part->second, "subdived"));
        int bmeshId = meshlite_bmesh_create(meshliteContext);
        if (subdived)
            meshlite_bmesh_set_cut_subdiv_count(meshliteContext, bmeshId, 1);
        QString thicknessString = valueOfKeyInMapOrEmpty(part->second, "deformThickness");
        if (!thicknessString.isEmpty())
            meshlite_bmesh_set_deform_thickness(meshliteContext, bmeshId, thicknessString.toFloat());
        QString widthString = valueOfKeyInMapOrEmpty(part->second, "deformWidth");
        if (!widthString.isEmpty())
            meshlite_bmesh_set_deform_width(meshliteContext, bmeshId, widthString.toFloat());
        if (MeshGenerator::enableDebug)
            meshlite_bmesh_enable_debug(meshliteContext, bmeshId, 1);
        partBmeshMap[partIdIt] = bmeshId;
    }
    
    for (const auto &edgeIt: m_snapshot->edges) {
        QString partId = valueOfKeyInMapOrEmpty(edgeIt.second, "partId");
        QString fromNodeId = valueOfKeyInMapOrEmpty(edgeIt.second, "from");
        QString toNodeId = valueOfKeyInMapOrEmpty(edgeIt.second, "to");
        //qDebug() << "Processing edge " << fromNodeId << "<=>" << toNodeId;
        const auto fromIt = m_snapshot->nodes.find(fromNodeId);
        const auto toIt = m_snapshot->nodes.find(toNodeId);
        if (fromIt == m_snapshot->nodes.end() || toIt == m_snapshot->nodes.end())
            continue;
        const auto partBmeshIt = partBmeshMap.find(partId);
        if (partBmeshIt == partBmeshMap.end())
            continue;
        int bmeshId = partBmeshIt->second;
        
        int bmeshFromNodeId = 0;
        const auto bmeshFromIt = bmeshNodeMap.find(fromNodeId);
        if (bmeshFromIt == bmeshNodeMap.end()) {
            float radius = valueOfKeyInMapOrEmpty(fromIt->second, "radius").toFloat() / longHeight;
            float x = (valueOfKeyInMapOrEmpty(fromIt->second, "x").toFloat() - mainProfileMiddleX) / longHeight;
            float y = (mainProfileMiddleY - valueOfKeyInMapOrEmpty(fromIt->second, "y").toFloat()) / longHeight;
            float z = (sideProfileMiddleX - valueOfKeyInMapOrEmpty(fromIt->second, "z").toFloat()) / longHeight;
            bmeshFromNodeId = meshlite_bmesh_add_node(meshliteContext, bmeshId, x, y, z, radius);
            //qDebug() << "bmeshId[" << bmeshId << "] add node[" << bmeshFromNodeId << "]" << radius << x << y << z;
            bmeshNodeMap[fromNodeId] = bmeshFromNodeId;
        } else {
            bmeshFromNodeId = bmeshFromIt->second;
            //qDebug() << "bmeshId[" << bmeshId << "] use existed node[" << bmeshFromNodeId << "]";
        }
        
        int bmeshToNodeId = 0;
        const auto bmeshToIt = bmeshNodeMap.find(toNodeId);
        if (bmeshToIt == bmeshNodeMap.end()) {
            float radius = valueOfKeyInMapOrEmpty(toIt->second, "radius").toFloat() / longHeight;
            float x = (valueOfKeyInMapOrEmpty(toIt->second, "x").toFloat() - mainProfileMiddleX) / longHeight;
            float y = (mainProfileMiddleY - valueOfKeyInMapOrEmpty(toIt->second, "y").toFloat()) / longHeight;
            float z = (sideProfileMiddleX - valueOfKeyInMapOrEmpty(toIt->second, "z").toFloat()) / longHeight;
            bmeshToNodeId = meshlite_bmesh_add_node(meshliteContext, bmeshId, x, y, z, radius);
            //qDebug() << "bmeshId[" << bmeshId << "] add node[" << bmeshToNodeId << "]" << radius << x << y << z;
            bmeshNodeMap[toNodeId] = bmeshToNodeId;
        } else {
            bmeshToNodeId = bmeshToIt->second;
            //qDebug() << "bmeshId[" << bmeshId << "] use existed node[" << bmeshToNodeId << "]";
        }
        
        meshlite_bmesh_add_edge(meshliteContext, bmeshId, bmeshFromNodeId, bmeshToNodeId);
    }
    
    for (const auto &nodeIt: m_snapshot->nodes) {
        QString partId = valueOfKeyInMapOrEmpty(nodeIt.second, "partId");
        const auto partBmeshIt = partBmeshMap.find(partId);
        if (partBmeshIt == partBmeshMap.end())
            continue;
        const auto nodeBmeshIt = bmeshNodeMap.find(nodeIt.first);
        if (nodeBmeshIt != bmeshNodeMap.end())
            continue;
        int bmeshId = partBmeshIt->second;
        float radius = valueOfKeyInMapOrEmpty(nodeIt.second, "radius").toFloat() / longHeight;
        float x = (valueOfKeyInMapOrEmpty(nodeIt.second, "x").toFloat() - mainProfileMiddleX) / longHeight;
        float y = (mainProfileMiddleY - valueOfKeyInMapOrEmpty(nodeIt.second, "y").toFloat()) / longHeight;
        float z = (sideProfileMiddleX - valueOfKeyInMapOrEmpty(nodeIt.second, "z").toFloat()) / longHeight;
        int bmeshNodeId = meshlite_bmesh_add_node(meshliteContext, bmeshId, x, y, z, radius);
        //qDebug() << "bmeshId[" << bmeshId << "] add lonely node[" << bmeshNodeId << "]" << radius << x << y << z;
        bmeshNodeMap[nodeIt.first] = bmeshNodeId;
    }
    
    bool broken = false;
    
    std::vector<int> meshIds;
    std::vector<int> subdivMeshIds;
    for (const auto &partIdIt: m_snapshot->partIdList) {
        const auto &part = m_snapshot->parts.find(partIdIt);
        if (part == m_snapshot->parts.end())
            continue;
        QString disabledString = valueOfKeyInMapOrEmpty(part->second, "disabled");
        bool isDisabled = isTrueValueString(disabledString);
        if (isDisabled)
            continue;
        int bmeshId = partBmeshMap[partIdIt];
        int meshId = meshlite_bmesh_generate_mesh(meshliteContext, bmeshId);
        if (meshlite_bmesh_error_count(meshliteContext, bmeshId) != 0)
            broken = true;
        bool xMirrored = isTrueValueString(valueOfKeyInMapOrEmpty(part->second, "xMirrored"));
        bool zMirrored = isTrueValueString(valueOfKeyInMapOrEmpty(part->second, "zMirrored"));
        int xMirroredMeshId = 0;
        int zMirroredMeshId = 0;
        if (xMirrored || zMirrored) {
            if (xMirrored) {
                xMirroredMeshId = meshlite_mirror_in_x(meshliteContext, meshId, 0);
            }
            if (zMirrored) {
                zMirroredMeshId = meshlite_mirror_in_z(meshliteContext, meshId, 0);
            }
        }
        if (m_requirePartPreviewMap.find(partIdIt) != m_requirePartPreviewMap.end()) {
            ModelOfflineRender *render = m_partPreviewRenderMap[partIdIt];
            int trimedMeshId = meshlite_trim(meshliteContext, meshId, 1);
            render->updateMesh(new Mesh(meshliteContext, trimedMeshId));
            QImage *image = new QImage(render->toImage(QSize(Theme::previewImageSize, Theme::previewImageSize)));
            m_partPreviewMap[partIdIt] = image;
        }
        meshIds.push_back(meshId);
        if (xMirroredMeshId)
            meshIds.push_back(xMirroredMeshId);
        if (zMirroredMeshId)
            meshIds.push_back(zMirroredMeshId);
    }
    
    if (!subdivMeshIds.empty()) {
        int mergedMeshId = 0;
        if (subdivMeshIds.size() > 1) {
            int errorCount = 0;
            mergedMeshId = unionMeshs(meshliteContext, subdivMeshIds, &errorCount);
            if (errorCount)
                broken = true;
        } else {
            mergedMeshId = subdivMeshIds[0];
        }
        //if (mergedMeshId > 0)
        //    mergedMeshId = meshlite_combine_coplanar_faces(meshliteContext, mergedMeshId);
        if (mergedMeshId > 0) {
            int errorCount = 0;
            int subdivedMeshId = subdivMesh(meshliteContext, mergedMeshId, &errorCount);
            if (errorCount > 0)
                broken = true;
            if (subdivedMeshId > 0)
                mergedMeshId = subdivedMeshId;
            else
                broken = true;
        }
        if (mergedMeshId > 0)
            meshIds.push_back(mergedMeshId);
        else
            broken = true;
    }
    
    int mergedMeshId = 0;
    if (meshIds.size() > 1) {
        int errorCount = 0;
        mergedMeshId = unionMeshs(meshliteContext, meshIds, &errorCount);
        if (errorCount)
            broken = true;
        else if (mergedMeshId > 0)
            mergedMeshId = meshlite_combine_coplanar_faces(meshliteContext, mergedMeshId);
            if (mergedMeshId > 0)
                mergedMeshId = meshlite_fix_hole(meshliteContext, mergedMeshId);
    } else if (meshIds.size() > 0) {
        mergedMeshId = meshIds[0];
    }
    
    if (mergedMeshId > 0) {
        if (m_requirePreview) {
            m_previewRender->updateMesh(new Mesh(meshliteContext, mergedMeshId));
            QImage *image = new QImage(m_previewRender->toImage(QSize(Theme::previewImageSize, Theme::previewImageSize)));
            m_preview = image;
        }
        int finalMeshId = mergedMeshId;
        if (!originSettled) {
            finalMeshId = meshlite_trim(meshliteContext, mergedMeshId, 1);
        }
        m_mesh = new Mesh(meshliteContext, finalMeshId, broken);
    }
    
    if (m_previewRender) {
        m_previewRender->setRenderThread(QGuiApplication::instance()->thread());
    }
    
    for (auto &partPreviewRender: m_partPreviewRenderMap) {
        partPreviewRender.second->setRenderThread(QGuiApplication::instance()->thread());
    }
    
    meshlite_destroy_context(meshliteContext);
    
    this->moveToThread(QGuiApplication::instance()->thread());
    
    emit finished();
}
