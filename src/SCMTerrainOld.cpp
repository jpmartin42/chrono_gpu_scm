// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Alessandro Tasora, Radu Serban, Jay Taves
// =============================================================================
//
// Deformable terrain based on SCM (Soil Contact Model) from DLR
// (Krenn & Hirzinger)
//
// =============================================================================

#include <cstdio>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <limits>

#ifdef _OPENMP
    #include <omp.h>
#endif

#include "chrono/physics/ChContactMaterialNSC.h"
#include "chrono/physics/ChContactMaterialSMC.h"
#include "chrono/fea/ChContactSurfaceMesh.h"
#include "chrono/assets/ChTexture.h"
#include "chrono/assets/ChVisualShapeBox.h"
#include "chrono/utils/ChConvexHull.h"
#include "chrono/utils/ChUtils.h"

#include "chrono_vehicle/ChVehicleModelData.h"
#include "chrono_vehicle/terrain/SCMTerrain.h"

#include "chrono_thirdparty/stb/stb.h"

namespace chrono {
namespace vehicle {

// -----------------------------------------------------------------------------
// Implementation of the SCMTerrain wrapper class
// -----------------------------------------------------------------------------

SCMTerrainOld::SCMTerrainOld(ChSystem* system, bool visualization_mesh) {
    if (!system->GetCollisionSystem()) {
        std::cerr << "\nError: SCMTerrain requires collision detection.\n";
        std::cerr << "A collision system must be associated to the Chrono system before constructing the SCMTerrain."
                  << std::endl;
        throw std::runtime_error("SCMTerrain requires a collision system be associated with the Chrono system.");
    }
    m_loader = chrono_types::make_shared<SCMLoaderOld>(system, visualization_mesh);
    system->Add(m_loader);
}

// Get the initial terrain height below the specified location.
double SCMTerrainOld::GetInitHeight(const ChVector3d& loc) const {
    return m_loader->GetInitHeight(loc);
}

// Get the initial terrain normal at the point below the specified location.
ChVector3d SCMTerrainOld::GetInitNormal(const ChVector3d& loc) const {
    return m_loader->GetInitNormal(loc);
}

// Get the terrain height below the specified location.
double SCMTerrainOld::GetHeight(const ChVector3d& loc) const {
    return m_loader->GetHeight(loc);
}

// Get the terrain normal at the point below the specified location.
ChVector3d SCMTerrainOld::GetNormal(const ChVector3d& loc) const {
    return m_loader->GetNormal(loc);
}

// Return the terrain coefficient of friction at the specified location.
float SCMTerrainOld::GetCoefficientFriction(const ChVector3d& loc) const {
    return m_friction_fun ? (*m_friction_fun)(loc) : 0.8f;
}

// Get SCM information at the node closest to the specified location.
SCMTerrainOld::NodeInfo SCMTerrainOld::GetNodeInfo(const ChVector3d& loc) const {
    return m_loader->GetNodeInfo(loc);
}

// Set the color of the visualization assets.
void SCMTerrainOld::SetColor(const ChColor& color) {
    if (m_loader->GetVisualModel()) {
        m_loader->GetVisualShape(0)->SetColor(color);
    }
}

// Set the texture and texture scaling.
void SCMTerrainOld::SetTexture(const std::string tex_file, float scale_x, float scale_y) {
    if (m_loader->GetVisualModel()) {
        m_loader->GetVisualShape(0)->SetTexture(tex_file, scale_x, scale_y);
    }
}

// Set the SCM reference plane.
void SCMTerrainOld::SetReferenceFrame(const ChCoordsys<>& frame) {
    m_loader->m_frame = frame;
    m_loader->m_Z = frame.rot.GetAxisZ();
}

// Get the SCM reference frame.
const ChCoordsys<>& SCMTerrainOld::GetReferenceFrame() const {
    return m_loader->m_frame;
}

// Set the visualization mesh as wireframe or as solid.
void SCMTerrainOld::SetMeshWireframe(bool val) {
    if (m_loader->m_trimesh_shape)
        m_loader->m_trimesh_shape->SetWireframe(val);
}

// Get the trimesh that defines the ground shape.
std::shared_ptr<ChVisualShapeTriangleMesh> SCMTerrainOld::GetMesh() const {
    return m_loader->m_trimesh_shape;
}

// Save the visualization mesh as a Wavefront OBJ file.
void SCMTerrainOld::WriteMesh(const std::string& filename) const {
    if (!m_loader->m_trimesh_shape) {
        std::cout << "SCMTerrainOld::WriteMesh  -- visualization mesh not created.";
        return;
    }
    auto trimesh = m_loader->m_trimesh_shape->GetMesh();
    std::vector<ChTriangleMeshConnected> meshes = {*trimesh};
    trimesh->WriteWavefront(filename, meshes);
}

// Enable/disable co-simulation mode.
void SCMTerrainOld::SetCosimulationMode(bool val) {
    m_loader->m_cosim_mode = val;
}

// Set properties of the SCM soil model.
void SCMTerrainOld::SetSoilParameters(
    double Bekker_Kphi,    // Kphi, frictional modulus in Bekker model
    double Bekker_Kc,      // Kc, cohesive modulus in Bekker model
    double Bekker_n,       // n, exponent of sinkage in Bekker model (usually 0.6...1.8)
    double Mohr_cohesion,  // Cohesion for shear failure [Pa]
    double Mohr_friction,  // Friction angle for shear failure [degree]
    double Janosi_shear,   // Shear parameter in Janosi-Hanamoto formula [m]
    double elastic_K,      // elastic stiffness K per unit area, [Pa/m] (must be larger than Kphi)
    double damping_R       // vertical damping R per unit area [Pa.s/m] (proportional to vertical speed)
) {
    m_loader->m_Bekker_Kphi = Bekker_Kphi;
    m_loader->m_Bekker_Kc = Bekker_Kc;
    m_loader->m_Bekker_n = Bekker_n;
    m_loader->m_Mohr_cohesion = Mohr_cohesion;
    m_loader->m_Mohr_mu = std::tan(Mohr_friction * CH_DEG_TO_RAD);
    m_loader->m_Janosi_shear = Janosi_shear;
    m_loader->m_elastic_K = std::max(elastic_K, Bekker_Kphi);
    m_loader->m_damping_R = damping_R;
}

// Enable/disable bulldozing effect.
void SCMTerrainOld::EnableBulldozing(bool val) {
    m_loader->m_bulldozing = val;
}

// Set parameters controlling the creation of side ruts (bulldozing effects).
void SCMTerrainOld::SetBulldozingParameters(
    double erosion_angle,     // angle of erosion of the displaced material [degrees]
    double flow_factor,       // growth of lateral volume relative to pressed volume
    int erosion_iterations,   // number of erosion refinements per timestep
    int erosion_propagations  // number of concentric vertex selections subject to erosion
) {
    m_loader->m_flow_factor = flow_factor;
    m_loader->m_erosion_slope = std::tan(erosion_angle * CH_DEG_TO_RAD);
    m_loader->m_erosion_iterations = erosion_iterations;
    m_loader->m_erosion_propagations = erosion_propagations;
}

void SCMTerrainOld::SetTestHeight(double offset) {
    m_loader->m_test_offset_up = offset;
}

double SCMTerrainOld::GetTestHeight() const {
    return m_loader->m_test_offset_up;
}

// Set the color plot type
void SCMTerrainOld::SetPlotType(DataPlotType plot_type, double min_val, double max_val) {
    m_loader->m_plot_type = plot_type;
    m_loader->m_plot_v_min = min_val;
    m_loader->m_plot_v_max = max_val;
}

// Set the colormap type
void SCMTerrainOld::SetColormap(ChColormap::Type type) {
    m_loader->m_colormap_type = type;
    if (m_loader->m_colormap) {
        m_loader->m_colormap->Load(type);
    }
}

// Get the current colormap
const ChColormap& SCMTerrainOld::GetColormap() const {
    return *m_loader->m_colormap;
}

ChColormap::Type SCMTerrainOld::GetColormapType() const {
    return m_loader->m_colormap_type;
}

// Enable SCM terrain patch boundaries
void SCMTerrainOld::SetBoundary(const ChAABB& aabb) {
    if (aabb.IsInverted())
        return;

    m_loader->m_aabb = aabb;
    m_loader->m_boundary = true;
}

// Add a user-provided active domains
void SCMTerrainOld::AddActiveDomain(std::shared_ptr<ChBody> body,
                                 const ChVector3d& OOBB_center,
                                 const ChVector3d& OOBB_dims) {
    SCMLoaderOld::ActiveDomainInfo ad;
    ad.m_body = body;
    ad.m_center = OOBB_center;
    ad.m_hdims = OOBB_dims / 2;

    m_loader->m_active_domains.push_back(ad);

    // Enable user-provided active domains
    m_loader->m_user_domains = true;
}

// Set user-supplied callback for evaluating location-dependent soil parameters.
void SCMTerrainOld::RegisterSoilParametersCallback(std::shared_ptr<SoilParametersCallback> cb) {
    m_loader->m_soil_fun = cb;
}

// Initialize the terrain as a flat grid.
void SCMTerrainOld::Initialize(double sizeX, double sizeY, double delta) {
    m_loader->Initialize(sizeX, sizeY, delta);
}

// Initialize the terrain from a specified height map.
void SCMTerrainOld::Initialize(const std::string& heightmap_file,
                            double sizeX,
                            double sizeY,
                            double hMin,
                            double hMax,
                            double delta) {
    m_loader->Initialize(heightmap_file, sizeX, sizeY, hMin, hMax, delta);
}

// Initialize the terrain from a specified OBJ mesh file.
void SCMTerrainOld::Initialize(const std::string& mesh_file, double delta) {
    m_loader->Initialize(mesh_file, delta);
}

// Initialize the terrain from a specified triangular mesh file.
void SCMTerrainOld::Initialize(const ChTriangleMeshConnected& trimesh, double delta) {
    m_loader->Initialize(trimesh, delta);
}

// Get the heights of modified grid nodes.
std::vector<SCMTerrainOld::NodeLevel> SCMTerrainOld::GetModifiedNodes(bool all_nodes) const {
    return m_loader->GetModifiedNodes(all_nodes);
}

// Modify the level of grid nodes from the given list.
void SCMTerrainOld::SetModifiedNodes(const std::vector<NodeLevel>& nodes) {
    m_loader->SetModifiedNodes(nodes);
}

bool SCMTerrainOld::GetContactForceBody(std::shared_ptr<ChBody> body, ChVector3d& force, ChVector3d& torque) const {
    auto itr = m_loader->m_body_forces.find(body.get());
    if (itr == m_loader->m_body_forces.end()) {
        force = VNULL;
        torque = VNULL;
        return false;
    }

    force = itr->second.first;
    torque = itr->second.second;
    return true;
}

bool SCMTerrainOld::GetContactForceNode(std::shared_ptr<fea::ChNodeFEAxyz> node, ChVector3d& force) const {
    auto itr = m_loader->m_node_forces.find(node);
    if (itr == m_loader->m_node_forces.end()) {
        force = VNULL;
        return false;
    }

    force = itr->second;
    return true;
}

// Return the number of rays cast at last step.
int SCMTerrainOld::GetNumRayCasts() const {
    return m_loader->m_num_ray_casts;
}

// Return the number of ray hits at last step.
int SCMTerrainOld::GetNumRayHits() const {
    return m_loader->m_num_ray_hits;
}

// Return the number of contact patches at last step.
int SCMTerrainOld::GetNumContactPatches() const {
    return m_loader->m_num_contact_patches;
}

// Return the number of nodes in the erosion domain at last step (bulldosing effects).
int SCMTerrainOld::GetNumErosionNodes() const {
    return m_loader->m_num_erosion_nodes;
}

// Timer information
double SCMTerrainOld::GetTimerActiveDomains() const {
    return 1e3 * m_loader->m_timer_active_domains();
}
double SCMTerrainOld::GetTimerRayTesting() const {
    return 1e3 * m_loader->m_timer_ray_testing();
}
double SCMTerrainOld::GetTimerRayCasting() const {
    return 1e3 * m_loader->m_timer_ray_casting();
}
double SCMTerrainOld::GetTimerContactPatches() const {
    return 1e3 * m_loader->m_timer_contact_patches();
}
double SCMTerrainOld::GetTimerContactForces() const {
    return 1e3 * m_loader->m_timer_contact_forces();
}
double SCMTerrainOld::GetTimerBulldozing() const {
    return 1e3 * m_loader->m_timer_bulldozing();
}
double SCMTerrainOld::GetTimerVisUpdate() const {
    return 1e3 * m_loader->m_timer_visualization();
}

void SCMTerrainOld::SetBaseMeshLevel(double level) {
    m_loader->m_base_height = level;
}

// Print timing and counter information for last step.
void SCMTerrainOld::PrintStepStatistics(std::ostream& os) const {
    os << " Timers (ms):" << std::endl;
    os << "   Moving patches:          " << 1e3 * m_loader->m_timer_active_domains() << std::endl;
    os << "   Ray testing:             " << 1e3 * m_loader->m_timer_ray_testing() << std::endl;
    os << "   Ray casting:             " << 1e3 * m_loader->m_timer_ray_casting() << std::endl;
    os << "   Contact patches:         " << 1e3 * m_loader->m_timer_contact_patches() << std::endl;
    os << "   Contact forces:          " << 1e3 * m_loader->m_timer_contact_forces() << std::endl;
    os << "   Bulldozing:              " << 1e3 * m_loader->m_timer_bulldozing() << std::endl;
    os << "      Raise boundary:       " << 1e3 * m_loader->m_timer_bulldozing_boundary() << std::endl;
    os << "      Compute domain:       " << 1e3 * m_loader->m_timer_bulldozing_domain() << std::endl;
    os << "      Apply erosion:        " << 1e3 * m_loader->m_timer_bulldozing_erosion() << std::endl;
    os << "   Visualization:           " << 1e3 * m_loader->m_timer_visualization() << std::endl;

    os << " Counters:" << std::endl;
    os << "   Number ray casts:        " << m_loader->m_num_ray_casts << std::endl;
    os << "   Number ray hits:         " << m_loader->m_num_ray_hits << std::endl;
    os << "   Number contact patches:  " << m_loader->m_num_contact_patches << std::endl;
    os << "   Number erosion nodes:    " << m_loader->m_num_erosion_nodes << std::endl;
}

// -----------------------------------------------------------------------------
// Contactable user-data (contactable-soil parameters)
// -----------------------------------------------------------------------------

SCMContactableData::SCMContactableData(double area_ratio,
                                       double Mohr_cohesion,
                                       double Mohr_friction,
                                       double Janosi_shear)
    : area_ratio(area_ratio),
      Mohr_cohesion(Mohr_cohesion),
      Mohr_mu(std::tan(Mohr_friction * CH_DEG_TO_RAD)),
      Janosi_shear(Janosi_shear) {}

// -----------------------------------------------------------------------------
// Implementation of SCMLoaderOld
// -----------------------------------------------------------------------------

// Constructor.
SCMLoaderOld::SCMLoaderOld(ChSystem* system, bool visualization_mesh) : m_soil_fun(nullptr), m_base_height(-1000) {
    this->SetSystem(system);

    if (visualization_mesh) {
        // Create the visualization mesh and asset
        m_trimesh_shape = std::shared_ptr<ChVisualShapeTriangleMesh>(new ChVisualShapeTriangleMesh);
        m_trimesh_shape->SetWireframe(true);
        m_trimesh_shape->SetFixedConnectivity();
    }

    // Default SCM plane and plane normal
    m_frame = ChCoordsys<>(VNULL, QUNIT);
    m_Z = m_frame.rot.GetAxisZ();

    // Bulldozing effects
    m_bulldozing = false;
    m_flow_factor = 1.2;
    m_erosion_slope = std::tan(40.0 * CH_DEG_TO_RAD);
    m_erosion_iterations = 3;
    m_erosion_propagations = 10;

    // Default soil parameters
    m_Bekker_Kphi = 2e6;
    m_Bekker_Kc = 0;
    m_Bekker_n = 1.1;
    m_Mohr_cohesion = 50;
    m_Mohr_mu = std::tan(20.0 * CH_DEG_TO_RAD);
    m_Janosi_shear = 0.01;
    m_elastic_K = 50000000;
    m_damping_R = 0;

    m_colormap_type = ChColormap::Type::JET;
    m_plot_type = SCMTerrainOld::PLOT_NONE;
    m_plot_v_min = 0;
    m_plot_v_max = 0.2;

    m_test_offset_up = 0.1;
    m_test_offset_down = 0.5;

    m_boundary = false;
    m_user_domains = false;
    m_cosim_mode = false;
}

// Initialize the terrain as a flat grid
void SCMLoaderOld::Initialize(double sizeX, double sizeY, double delta) {
    m_type = PatchType::FLAT;

    m_nx = static_cast<int>(std::ceil((sizeX / 2) / delta));  // half number of divisions in X direction
    m_ny = static_cast<int>(std::ceil((sizeY / 2) / delta));  // number of divisions in Y direction

    m_delta = sizeX / (2 * m_nx);   // grid spacing
    m_area = std::pow(m_delta, 2);  // area of a cell

    // Return now if no visualization
    if (!m_trimesh_shape)
        return;

    CreateVisualizationMesh(sizeX, sizeY);
    this->AddVisualShape(m_trimesh_shape);
}

// Initialize the terrain from a specified height map.
void SCMLoaderOld::Initialize(const std::string& heightmap_file,
                           double sizeX,
                           double sizeY,
                           double hMin,
                           double hMax,
                           double delta) {
    m_type = PatchType::HEIGHT_MAP;

    // Read the image file (request only 1 channel) and extract number of pixels.
    STB hmap;
    if (!hmap.ReadFromFile(heightmap_file, 1)) {
        std::cerr << "STB error in reading height map file " << heightmap_file << std::endl;
        throw std::runtime_error("Cannot read height map image file");
    }
    int nx_img = hmap.GetWidth();
    int ny_img = hmap.GetHeight();

    double dx_img = 1.0 / (nx_img - 1.0);
    double dy_img = 1.0 / (ny_img - 1.0);

    m_nx = static_cast<int>(std::ceil((sizeX / 2) / delta));  // half number of divisions in X direction
    m_ny = static_cast<int>(std::ceil((sizeY / 2) / delta));  // number of divisions in Y direction
    int nvx = 2 * m_nx + 1;                                   // number of grid vertices in X direction
    int nvy = 2 * m_ny + 1;                                   // number of grid vertices in Y direction
    m_delta = sizeX / (2.0 * m_nx);                           // grid spacing
    m_area = std::pow(m_delta, 2);                            // area of a cell

    double dx_grid = 0.5 / m_nx;
    double dy_grid = 0.5 / m_ny;

    // Resample image and calculate interpolated gray levels and then map it to the height range, with black
    // corresponding to hMin and white corresponding to hMax. Entry (0,0) corresponds to bottom-left grid vertex.
    // Note that pixels in the image start at top-left corner.
    double h_scale = (hMax - hMin) / hmap.GetRange();
    m_heights = ChMatrixDynamic<>(nvx, nvy);
    for (int ix = 0; ix < nvx; ix++) {
        double x = ix * dx_grid;                  // x location in image (in [0,1], 0 at left)
        int jx1 = (int)std::floor(x / dx_img);    // Left pixel
        int jx2 = (int)std::ceil(x / dx_img);     // Right pixel
        double ax = (x - jx1 * dx_img) / dx_img;  // Scaled offset from left pixel

        assert(ax < 1.0);
        assert(jx1 < nx_img);
        assert(jx2 < nx_img);
        assert(jx1 <= jx2);

        for (int iy = 0; iy < nvy; iy++) {
            double y = (2 * m_ny - iy) * dy_grid;     // y location in image (in [0,1], 0 at top)
            int jy1 = (int)std::floor(y / dy_img);    // Up pixel
            int jy2 = (int)std::ceil(y / dy_img);     // Down pixel
            double ay = (y - jy1 * dy_img) / dy_img;  // Scaled offset from down pixel

            assert(ay < 1.0);
            assert(jy1 < ny_img);
            assert(jy2 < ny_img);
            assert(jy1 <= jy2);

            // Gray levels at left-up, left-down, right-up, and right-down pixels
            double g11 = hmap.Gray(jx1, jy1);
            double g12 = hmap.Gray(jx1, jy2);
            double g21 = hmap.Gray(jx2, jy1);
            double g22 = hmap.Gray(jx2, jy2);

            // Bilinear interpolation (gray level)
            m_heights(ix, iy) = (1 - ax) * (1 - ay) * g11 + (1 - ax) * ay * g12 + ax * (1 - ay) * g21 + ax * ay * g22;
            // Map into height range
            m_heights(ix, iy) = hMin + m_heights(ix, iy) * h_scale;
        }
    }

    // Return now if no visualization
    if (!m_trimesh_shape)
        return;

    CreateVisualizationMesh(sizeX, sizeY);
    this->AddVisualShape(m_trimesh_shape);
}

// Initialize the terrain from a specified OBJ mesh file.
bool calcBarycentricCoordinates(const ChVector3d& v1,
                                const ChVector3d& v2,
                                const ChVector3d& v3,
                                const ChVector3d& v,
                                double& a1,
                                double& a2,
                                double& a3) {
    double denom = (v2.y() - v3.y()) * (v1.x() - v3.x()) + (v3.x() - v2.x()) * (v1.y() - v3.y());
    a1 = ((v2.y() - v3.y()) * (v.x() - v3.x()) + (v3.x() - v2.x()) * (v.y() - v3.y())) / denom;
    a2 = ((v3.y() - v1.y()) * (v.x() - v3.x()) + (v1.x() - v3.x()) * (v.y() - v3.y())) / denom;
    a3 = 1 - a1 - a2;

    return (0 <= a1) && (a1 <= 1) && (0 <= a2) && (a2 <= 1) && (0 <= a3) && (a3 <= 1);
}

void SCMLoaderOld::Initialize(const std::string& mesh_file, double delta) {
    // Load triangular mesh
    auto trimesh = ChTriangleMeshConnected::CreateFromWavefrontFile(mesh_file, true, true);

    Initialize(*trimesh, delta);
}

void SCMLoaderOld::Initialize(const ChTriangleMeshConnected& trimesh, double delta) {
    m_type = PatchType::TRI_MESH;

    // Load triangular mesh
    const auto& vertices = trimesh.GetCoordsVertices();
    const auto& faces = trimesh.GetIndicesVertexes();

    // Find x, y, and z ranges of vertex data
    auto minmaxX = std::minmax_element(begin(vertices), end(vertices),
                                       [](const ChVector3d& v1, const ChVector3d& v2) { return v1.x() < v2.x(); });
    auto minmaxY = std::minmax_element(begin(vertices), end(vertices),
                                       [](const ChVector3d& v1, const ChVector3d& v2) { return v1.y() < v2.y(); });
    auto minmaxZ = std::minmax_element(begin(vertices), end(vertices),
                                       [](const ChVector3d& v1, const ChVector3d& v2) { return v1.z() < v2.z(); });
    auto minX = minmaxX.first->x() + delta;
    auto maxX = minmaxX.second->x() - delta;
    auto minY = minmaxY.first->y() + delta;
    auto maxY = minmaxY.second->y() - delta;
    auto minZ = minmaxZ.first->z();
    ////auto maxZ = minmaxZ.second->z();

    auto sizeX = (maxX - minX);
    auto sizeY = (maxY - minY);
    ChVector3d center((maxX + minX) / 2, (maxY + minY) / 2, 0);

    // Initial grid extent
    m_nx = static_cast<int>(std::ceil((sizeX / 2) / delta));  // half number of divisions in X direction
    m_ny = static_cast<int>(std::ceil((sizeY / 2) / delta));  // number of divisions in Y direction
    m_delta = sizeX / (2.0 * m_nx);                           // grid spacing
    m_area = std::pow(m_delta, 2);                            // area of a cell
    int nvx = 2 * m_nx + 1;                                   // number of grid vertices in X direction
    int nvy = 2 * m_ny + 1;                                   // number of grid vertices in Y direction

    // Loop over all mesh faces, project onto the x-y plane and set the height for all covered grid nodes.
    ////m_heights = ChMatrixDynamic<>::Zero(nvx, nvy);
    m_heights = (minZ + m_base_height) * ChMatrixDynamic<>::Ones(nvx, nvy);

    int num_h_set = 0;
    double a1, a2, a3;
    for (const auto& f : faces) {
        // Find bounds of (shifted) face projection
        const auto& v1 = vertices[f[0]] - center;
        const auto& v2 = vertices[f[1]] - center;
        const auto& v3 = vertices[f[2]] - center;
        auto x_min = std::min(std::min(v1.x(), v2.x()), v3.x());
        auto x_max = std::max(std::max(v1.x(), v2.x()), v3.x());
        auto y_min = std::min(std::min(v1.y(), v2.y()), v3.y());
        auto y_max = std::max(std::max(v1.y(), v2.y()), v3.y());
        int i_min = static_cast<int>(std::floor(x_min / m_delta));
        int j_min = static_cast<int>(std::floor(y_min / m_delta));
        int i_max = static_cast<int>(std::ceil(x_max / m_delta));
        int j_max = static_cast<int>(std::ceil(y_max / m_delta));
        ChClampValue(i_min, -m_nx, +m_nx);
        ChClampValue(i_max, -m_nx, +m_nx);
        ChClampValue(j_min, -m_ny, +m_ny);
        ChClampValue(j_max, -m_ny, +m_ny);
        // Loop over all grid nodes within bounds
        for (int i = i_min; i <= i_max; i++) {
            for (int j = j_min; j <= j_max; j++) {
                ChVector3d v(i * m_delta, j * m_delta, 0);
                if (calcBarycentricCoordinates(v1, v2, v3, v, a1, a2, a3)) {
                    m_heights(m_nx + i, m_ny + j) = minZ + a1 * v1.z() + a2 * v2.z() + a3 * v3.z();
                    num_h_set++;
                }
            }
        }
    }

    // Return now if no visualization
    if (!m_trimesh_shape)
        return;

    CreateVisualizationMesh(sizeX, sizeY);
    this->AddVisualShape(m_trimesh_shape);
}

void SCMLoaderOld::CreateVisualizationMesh(double sizeX, double sizeY) {
    // Create the colormap
    m_colormap = chrono_types::make_unique<ChColormap>(m_colormap_type);

    int nvx = 2 * m_nx + 1;                     // number of grid vertices in X direction
    int nvy = 2 * m_ny + 1;                     // number of grid vertices in Y direction
    int n_verts = nvx * nvy;                    // total number of vertices for initial visualization trimesh
    int n_faces = 2 * (2 * m_nx) * (2 * m_ny);  // total number of faces for initial visualization trimesh
    double x_scale = 0.5 / m_nx;                // scale for texture coordinates (U direction)
    double y_scale = 0.5 / m_ny;                // scale for texture coordinates (V direction)

    // Readability aliases
    auto trimesh = m_trimesh_shape->GetMesh();
    trimesh->Clear();
    std::vector<ChVector3d>& vertices = trimesh->GetCoordsVertices();
    std::vector<ChVector3d>& normals = trimesh->GetCoordsNormals();
    std::vector<ChVector3i>& idx_vertices = trimesh->GetIndicesVertexes();
    std::vector<ChVector3i>& idx_normals = trimesh->GetIndicesNormals();
    std::vector<ChVector2d>& uv_coords = trimesh->GetCoordsUV();
    std::vector<ChColor>& colors = trimesh->GetCoordsColors();

    // Resize mesh arrays.
    vertices.resize(n_verts);
    normals.resize(n_verts);
    uv_coords.resize(n_verts);
    colors.resize(n_verts);
    idx_vertices.resize(n_faces);
    idx_normals.resize(n_faces);

    // Load mesh vertices.
    // We order the vertices starting at the bottom-left corner, row after row.
    // The bottom-left corner corresponds to the point (-sizeX/2, -sizeY/2).
    // UV coordinates are mapped in [0,1] x [0,1]. Use smoothed vertex normals.
    int iv = 0;
    for (int iy = 0; iy < nvy; iy++) {
        double y = iy * m_delta - 0.5 * sizeY;
        for (int ix = 0; ix < nvx; ix++) {
            double x = ix * m_delta - 0.5 * sizeX;
            if (m_type == PatchType::FLAT) {
                // Set vertex location
                vertices[iv] = m_frame * ChVector3d(x, y, 0);
                // Initialize vertex normal to Z up
                normals[iv] = m_frame.TransformDirectionLocalToParent(ChVector3d(0, 0, 1));
            } else {
                // Set vertex location
                vertices[iv] = m_frame * ChVector3d(x, y, m_heights(ix, iy));
                // Initialize vertex normal to zero (will be set later)
                normals[iv] = ChVector3d(0, 0, 0);
            }
            // Assign color white to all vertices
            colors[iv] = ChColor(1, 1, 1);
            // Set UV coordinates in [0,1] x [0,1]
            uv_coords[iv] = ChVector2d(ix * x_scale, iy * y_scale);
            ++iv;
        }
    }

    // Specify triangular faces (two at a time).
    // Specify the face vertices counter-clockwise.
    // Set the normal indices same as the vertex indices.
    int it = 0;
    for (int iy = 0; iy < nvy - 1; iy++) {
        for (int ix = 0; ix < nvx - 1; ix++) {
            int v0 = ix + nvx * iy;
            idx_vertices[it] = ChVector3i(v0, v0 + 1, v0 + nvx + 1);
            idx_normals[it] = ChVector3i(v0, v0 + 1, v0 + nvx + 1);
            ++it;
            idx_vertices[it] = ChVector3i(v0, v0 + nvx + 1, v0 + nvx);
            idx_normals[it] = ChVector3i(v0, v0 + nvx + 1, v0 + nvx);
            ++it;
        }
    }

    if (m_type == PatchType::FLAT)
        return;

    // Initialize the array of accumulators (number of adjacent faces to a vertex)
    std::vector<int> accumulators(n_verts, 0);

    // Calculate normals and then average the normals from all adjacent faces.
    for (it = 0; it < n_faces; it++) {
        // Calculate the triangle normal as a normalized cross product.
        ChVector3d nrm = Vcross(vertices[idx_vertices[it][1]] - vertices[idx_vertices[it][0]],
                                vertices[idx_vertices[it][2]] - vertices[idx_vertices[it][0]]);
        nrm.Normalize();
        // Increment the normals of all incident vertices by the face normal
        normals[idx_normals[it][0]] += nrm;
        normals[idx_normals[it][1]] += nrm;
        normals[idx_normals[it][2]] += nrm;
        // Increment the count of all incident vertices by 1
        accumulators[idx_normals[it][0]] += 1;
        accumulators[idx_normals[it][1]] += 1;
        accumulators[idx_normals[it][2]] += 1;
    }

    // Set the normals to the average values.
    for (int in = 0; in < n_verts; in++) {
        normals[in] /= (double)accumulators[in];
    }
}

void SCMLoaderOld::SetupInitial() {
    // If no user-specified active domains, create one that will encompass all collision shapes in the system
    if (!m_user_domains) {
        SCMLoaderOld::ActiveDomainInfo ad;
        ad.m_body = nullptr;
        ad.m_center = {0, 0, 0};
        ad.m_hdims = {0.1, 0.1, 0.1};
        m_active_domains.push_back(ad);
    }
}

bool SCMLoaderOld::CheckMeshBounds(const ChVector2i& loc) const {
    return loc.x() >= -m_nx && loc.x() <= m_nx && loc.y() >= -m_ny && loc.y() <= m_ny;
}

SCMTerrainOld::NodeInfo SCMLoaderOld::GetNodeInfo(const ChVector3d& loc) const {
    SCMTerrainOld::NodeInfo ni;

    // Express location in the SCM frame
    ChVector3d loc_loc = m_frame.TransformPointParentToLocal(loc);

    // Find closest grid vertex (approximation)
    int i = static_cast<int>(std::round(loc_loc.x() / m_delta));
    int j = static_cast<int>(std::round(loc_loc.y() / m_delta));
    ChVector2i ij(i, j);

    // First query the hash-map
    auto p = m_grid_map.find(ij);
    if (p != m_grid_map.end()) {
        ni.sinkage = p->second.sinkage;
        ni.sinkage_plastic = p->second.sinkage_plastic;
        ni.sinkage_elastic = p->second.sinkage_elastic;
        ni.sigma = p->second.sigma;
        ni.sigma_yield = p->second.sigma_yield;
        ni.kshear = p->second.kshear;
        ni.tau = p->second.tau;
        return ni;
    }

    // Return a default node record
    ni.sinkage = 0;
    ni.sinkage_plastic = 0;
    ni.sinkage_elastic = 0;
    ni.sigma = 0;
    ni.sigma_yield = 0;
    ni.kshear = 0;
    ni.tau = 0;
    return ni;
}

// Get index of trimesh vertex corresponding to the specified grid vertex.
int SCMLoaderOld::GetMeshVertexIndex(const ChVector2i& loc) {
    assert(loc.x() >= -m_nx);
    assert(loc.x() <= +m_nx);
    assert(loc.y() >= -m_ny);
    assert(loc.y() <= +m_ny);
    return (loc.x() + m_nx) + (2 * m_nx + 1) * (loc.y() + m_ny);
}

// Get indices of trimesh faces incident to the specified grid vertex.
std::vector<int> SCMLoaderOld::GetMeshFaceIndices(const ChVector2i& loc) {
    int i = loc.x();
    int j = loc.y();

    // Ignore boundary vertices
    if (i == -m_nx || i == m_nx || j == -m_ny || j == m_ny)
        return std::vector<int>();

    // Load indices of 6 adjacent faces
    i += m_nx;
    j += m_ny;
    int nx = 2 * m_nx;
    std::vector<int> faces(6);
    faces[0] = 2 * ((i - 1) + nx * (j - 1));
    faces[1] = 2 * ((i - 1) + nx * (j - 1)) + 1;
    faces[2] = 2 * ((i - 1) + nx * (j - 0));
    faces[3] = 2 * ((i - 0) + nx * (j - 0));
    faces[4] = 2 * ((i - 0) + nx * (j - 0)) + 1;
    faces[5] = 2 * ((i - 0) + nx * (j - 1)) + 1;

    return faces;
}

// Get the initial undeformed terrain height (relative to the SCM plane) at the specified grid vertex.
double SCMLoaderOld::GetInitHeight(const ChVector2i& loc) const {
    switch (m_type) {
        case PatchType::FLAT:
            return 0;
        case PatchType::HEIGHT_MAP:
        case PatchType::TRI_MESH: {
            auto x = ChClamp(loc.x(), -m_nx, +m_nx);
            auto y = ChClamp(loc.y(), -m_ny, +m_ny);
            return m_heights(x + m_nx, y + m_ny);
        }
        default:
            return 0;
    }
}

// Get the initial undeformed terrain normal (relative to the SCM plane) at the specified grid node.
ChVector3d SCMLoaderOld::GetInitNormal(const ChVector2i& loc) const {
    switch (m_type) {
        case PatchType::HEIGHT_MAP:
        case PatchType::TRI_MESH: {
            // Average normals of 4 triangular faces incident to given grid node
            auto hE = GetInitHeight(loc + ChVector2i(1, 0));  // east
            auto hW = GetInitHeight(loc - ChVector2i(1, 0));  // west
            auto hN = GetInitHeight(loc + ChVector2i(0, 1));  // north
            auto hS = GetInitHeight(loc - ChVector2i(0, 1));  // south
            return ChVector3d(hW - hE, hS - hN, 2 * m_delta).GetNormalized();
        }
        case PatchType::FLAT:
        default:
            return ChVector3d(0, 0, 1);
    }
}

// Get the terrain height (relative to the SCM plane) at the specified grid vertex.
double SCMLoaderOld::GetHeight(const ChVector2i& loc) const {
    // First query the hash-map
    auto p = m_grid_map.find(loc);
    if (p != m_grid_map.end())
        return p->second.level;

    // Else return undeformed height
    return GetInitHeight(loc);
}

// Get the terrain normal (relative to the SCM plane) at the specified grid vertex.
ChVector3d SCMLoaderOld::GetNormal(const ChVector2d& loc) const {
    switch (m_type) {
        case PatchType::HEIGHT_MAP:
        case PatchType::TRI_MESH: {
            // Average normals of 4 triangular faces incident to given grid node
            auto hE = GetHeight(loc + ChVector2i(1, 0));  // east
            auto hW = GetHeight(loc - ChVector2i(1, 0));  // west
            auto hN = GetHeight(loc + ChVector2i(0, 1));  // north
            auto hS = GetHeight(loc - ChVector2i(0, 1));  // south
            return ChVector3d(hW - hE, hS - hN, 2 * m_delta).GetNormalized();
        }
        case PatchType::FLAT:
        default:
            return ChVector3d(0, 0, 1);
    }
}

// Get the initial terrain height below the specified location.
double SCMLoaderOld::GetInitHeight(const ChVector3d& loc) const {
    // Express location in the SCM frame
    ChVector3d loc_loc = m_frame.TransformPointParentToLocal(loc);

    // Get height (relative to SCM plane) at closest grid vertex (approximation)
    int i = static_cast<int>(std::round(loc_loc.x() / m_delta));
    int j = static_cast<int>(std::round(loc_loc.y() / m_delta));
    loc_loc.z() = GetInitHeight(ChVector2i(i, j));

    // Express in global frame
    ChVector3d loc_abs = m_frame.TransformPointLocalToParent(loc_loc);
    return ChWorldFrame::Height(loc_abs);
}

// Get the initial terrain normal at the point below the specified location.
ChVector3d SCMLoaderOld::GetInitNormal(const ChVector3d& loc) const {
    // Express location in the SCM frame
    ChVector3d loc_loc = m_frame.TransformPointParentToLocal(loc);

    // Get height (relative to SCM plane) at closest grid vertex (approximation)
    int i = static_cast<int>(std::round(loc_loc.x() / m_delta));
    int j = static_cast<int>(std::round(loc_loc.y() / m_delta));
    auto nrm_loc = GetInitNormal(ChVector2i(i, j));

    // Express in global frame
    auto nrm_abs = m_frame.TransformDirectionLocalToParent(nrm_loc);
    return ChWorldFrame::FromISO(nrm_abs);
}

// Get the terrain height below the specified location.
double SCMLoaderOld::GetHeight(const ChVector3d& loc) const {
    // Express location in the SCM frame
    ChVector3d loc_loc = m_frame.TransformPointParentToLocal(loc);

    // Get height (relative to SCM plane) at closest grid vertex (approximation)
    int i = static_cast<int>(std::round(loc_loc.x() / m_delta));
    int j = static_cast<int>(std::round(loc_loc.y() / m_delta));
    loc_loc.z() = GetHeight(ChVector2i(i, j));

    // Express in global frame
    ChVector3d loc_abs = m_frame.TransformPointLocalToParent(loc_loc);
    return ChWorldFrame::Height(loc_abs);
}

// Get the terrain normal at the point below the specified location.
ChVector3d SCMLoaderOld::GetNormal(const ChVector3d& loc) const {
    // Express location in the SCM frame
    ChVector3d loc_loc = m_frame.TransformPointParentToLocal(loc);

    // Get height (relative to SCM plane) at closest grid vertex (approximation)
    int i = static_cast<int>(std::round(loc_loc.x() / m_delta));
    int j = static_cast<int>(std::round(loc_loc.y() / m_delta));
    auto nrm_loc = GetNormal(ChVector2i(i, j));

    // Express in global frame
    auto nrm_abs = m_frame.TransformDirectionLocalToParent(nrm_loc);
    return ChWorldFrame::FromISO(nrm_abs);
}

// Synchronize information for a user-provided active domain
void SCMLoaderOld::UpdateActiveDomain(ActiveDomainInfo& ad, const ChVector3d& Z) {
    ChVector2d p_min(+std::numeric_limits<double>::max());
    ChVector2d p_max(-std::numeric_limits<double>::max());

    // Loop over all corners of the OOBB
    for (int j = 0; j < 8; j++) {
        int ix = j % 2;
        int iy = (j / 2) % 2;
        int iz = (j / 4);

        // OOBB corner in body frame
        ChVector3d c_body = ad.m_center + ad.m_hdims * ChVector3d(2.0 * ix - 1, 2.0 * iy - 1, 2.0 * iz - 1);
        // OOBB corner in absolute frame
        ChVector3d c_abs = ad.m_body->GetFrameRefToAbs().TransformPointLocalToParent(c_body);
        // OOBB corner expressed in SCM frame
        ChVector3d c_scm = m_frame.TransformPointParentToLocal(c_abs);

        // Update AABB of patch projection onto SCM plane
        p_min.x() = std::min(p_min.x(), c_scm.x());
        p_min.y() = std::min(p_min.y(), c_scm.y());
        p_max.x() = std::max(p_max.x(), c_scm.x());
        p_max.y() = std::max(p_max.y(), c_scm.y());
    }

    // Find index ranges for grid vertices contained in the patch projection AABB
    int x_min = static_cast<int>(std::ceil(p_min.x() / m_delta));
    int y_min = static_cast<int>(std::ceil(p_min.y() / m_delta));
    int x_max = static_cast<int>(std::floor(p_max.x() / m_delta));
    int y_max = static_cast<int>(std::floor(p_max.y() / m_delta));
    int n_x = x_max - x_min + 1;
    int n_y = y_max - y_min + 1;

    ad.m_range.resize(n_x * n_y);
    for (int i = 0; i < n_x; i++) {
        for (int j = 0; j < n_y; j++) {
            ad.m_range[j * n_x + i] = ChVector2i(i + x_min, j + y_min);
        }
    }

    // Calculate inverse of SCM normal expressed in body frame (for optimization of ray-OBB test)
    ChVector3d dir = ad.m_body->TransformDirectionParentToLocal(Z);
    ad.m_ooN.x() = (dir.x() == 0) ? 1e10 : 1.0 / dir.x();
    ad.m_ooN.y() = (dir.y() == 0) ? 1e10 : 1.0 / dir.y();
    ad.m_ooN.z() = (dir.z() == 0) ? 1e10 : 1.0 / dir.z();
}

// Synchronize information for the default active domain
void SCMLoaderOld::UpdateDefaultActiveDomain(ActiveDomainInfo& ad) {
    ChVector2d p_min(+std::numeric_limits<double>::max());
    ChVector2d p_max(-std::numeric_limits<double>::max());

    // Get current bounding box (AABB) of all collision shapes
    auto aabb = GetSystem()->GetCollisionSystem()->GetBoundingBox();

    ad.m_center = aabb.Center();
    ad.m_hdims = aabb.Size() / 2;

    // Loop over all corners of the AABB
    for (int j = 0; j < 8; j++) {
        int ix = j % 2;
        int iy = (j / 2) % 2;
        int iz = (j / 4);

        // AABB corner in absolute frame
        ChVector3d c_abs = aabb.max * ChVector3d(ix, iy, iz) + aabb.min * ChVector3d(1.0 - ix, 1.0 - iy, 1.0 - iz);
        // AABB corner in SCM frame
        ChVector3d c_scm = m_frame.TransformPointParentToLocal(c_abs);

        // Update AABB of patch projection onto SCM plane
        p_min.x() = std::min(p_min.x(), c_scm.x());
        p_min.y() = std::min(p_min.y(), c_scm.y());
        p_max.x() = std::max(p_max.x(), c_scm.x());
        p_max.y() = std::max(p_max.y(), c_scm.y());
    }

    // Find index ranges for grid vertices contained in the patch projection AABB
    int x_min = static_cast<int>(std::ceil(p_min.x() / m_delta));
    int y_min = static_cast<int>(std::ceil(p_min.y() / m_delta));
    int x_max = static_cast<int>(std::floor(p_max.x() / m_delta));
    int y_max = static_cast<int>(std::floor(p_max.y() / m_delta));
    int n_x = x_max - x_min + 1;
    int n_y = y_max - y_min + 1;

    ad.m_range.resize(n_x * n_y);
    for (int i = 0; i < n_x; i++) {
        for (int j = 0; j < n_y; j++) {
            ad.m_range[j * n_x + i] = ChVector2i(i + x_min, j + y_min);
        }
    }
}

// Ray-OBB intersection test
bool SCMLoaderOld::RayOBBtest(const ActiveDomainInfo& p, const ChVector3d& from, const ChVector3d& Z) {
    // Express ray origin in OBB frame
    ChVector3d orig = p.m_body->GetFrameRefToAbs().TransformPointParentToLocal(from) - p.m_center;

    // Perform ray-AABB test (slab tests)
    double t1 = (-p.m_hdims.x() - orig.x()) * p.m_ooN.x();
    double t2 = (+p.m_hdims.x() - orig.x()) * p.m_ooN.x();
    double t3 = (-p.m_hdims.y() - orig.y()) * p.m_ooN.y();
    double t4 = (+p.m_hdims.y() - orig.y()) * p.m_ooN.y();
    double t5 = (-p.m_hdims.z() - orig.z()) * p.m_ooN.z();
    double t6 = (+p.m_hdims.z() - orig.z()) * p.m_ooN.z();

    double tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
    double tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

    if (tmax < 0)
        return false;
    if (tmin > tmax)
        return false;
    return true;
}

// Offsets for the 8 neighbors of a grid vertex
static const std::vector<ChVector2i> neighbors8{
    ChVector2i(-1, -1),  // SW
    ChVector2i(0, -1),   // S
    ChVector2i(1, -1),   // SE
    ChVector2i(-1, 0),   // W
    ChVector2i(1, 0),    // E
    ChVector2i(-1, 1),   // NW
    ChVector2i(0, 1),    // N
    ChVector2i(1, 1)     // NE
};

static const std::vector<ChVector2i> neighbors4{
    ChVector2i(0, -1),  // S
    ChVector2i(-1, 0),  // W
    ChVector2i(1, 0),   // E
    ChVector2i(0, 1)    // N
};

// Default implementation uses Map-Reduce for collecting ray intersection hits.
// The alternative is to simultaneously load the global map of hits while ray casting (using a critical section).
////#define RAY_CASTING_WITH_CRITICAL_SECTION

// Reset the list of forces, and fills it with forces from a soil contact model.
void SCMLoaderOld::ComputeInternalForces() {
    // Initialize list of modified visualization mesh vertices (use any externally modified vertices)
    std::vector<int> modified_vertices = m_external_modified_vertices;
    m_external_modified_vertices.clear();

    // Reset quantities at grid nodes modified over previous step
    // (required for bulldozing effects and for proper visualization coloring)
    for (const auto& ij : m_modified_nodes) {
        auto& nr = m_grid_map.at(ij);
        nr.sigma = 0;
        nr.sinkage_elastic = 0;
        nr.step_plastic_flow = 0;
        nr.erosion = false;
        nr.hit_level = 1e9;

        // Update visualization (only color changes relevant here)
        if (m_trimesh_shape && CheckMeshBounds(ij)) {
            int iv = GetMeshVertexIndex(ij);          // mesh vertex index
            UpdateMeshVertexCoordinates(ij, iv, nr);  // update vertex coordinates and color
            modified_vertices.push_back(iv);
        }
    }

    m_modified_nodes.clear();

    // Reset timers
    m_timer_active_domains.reset();
    m_timer_ray_testing.reset();
    m_timer_ray_casting.reset();
    m_timer_contact_patches.reset();
    m_timer_contact_forces.reset();
    m_timer_bulldozing.reset();
    m_timer_bulldozing_boundary.reset();
    m_timer_bulldozing_domain.reset();
    m_timer_bulldozing_erosion.reset();
    m_timer_visualization.reset();

    // Reset the load list and map of contact forces
    this->GetLoadList().clear();
    m_body_forces.clear();
    m_node_forces.clear();

    // ---------------------
    // Update moving patches
    // ---------------------

    m_timer_active_domains.start();

    // Update active domains and find range of active grid indices)
    if (m_user_domains) {
        for (auto& a : m_active_domains)
            UpdateActiveDomain(a, m_Z);
    } else {
        assert(m_active_domains.size() == 1);
        UpdateDefaultActiveDomain(m_active_domains[0]);
    }

    m_timer_active_domains.stop();

    // -------------------------
    // Perform ray casting tests
    // -------------------------

    // Information of vertices with ray-cast hits
    struct HitRecord {
        ChContactable* contactable;  // pointer to hit object
        ChVector3d abs_point;        // hit point, expressed in global frame
        int patch_id;                // index of associated patch id
    };

    // Hash-map for vertices with ray-cast hits
    std::unordered_map<ChVector2i, HitRecord, CoordHash> hits;

    m_num_ray_casts = 0;
    m_num_ray_hits = 0;

    m_timer_ray_casting.start();

#ifdef RAY_CASTING_WITH_CRITICAL_SECTION

    int nthreads = GetSystem()->GetNumThreadsChrono();

    // Loop through all moving patches (user-defined or default one)
    for (auto& p : m_active_domains) {
        // Loop through all vertices in the patch range
        int num_ray_casts = 0;
    #pragma omp parallel for num_threads(nthreads) reduction(+ : num_ray_casts)
        for (int k = 0; k < p.m_range.size(); k++) {
            ChVector2i ij = p.m_range[k];

            // Move from (i, j) to (x, y, z) representation in the world frame
            double x = ij.x() * m_delta;
            double y = ij.y() * m_delta;
            double z;
    #pragma omp critical(SCM_ray_casting)
            z = GetHeight(ij);

            ChVector3d vertex_abs = m_frame.TransformPointLocalToParent(ChVector3d(x, y, z));

            // Create ray at current grid location
            ChCollisionSystem::ChRayhitResult mrayhit_result;
            ChVector3d to = vertex_abs + m_Z * m_test_offset_up;
            ChVector3d from = to - m_Z * m_test_offset_down;

            // Ray-OBB test (quick rejection)
            if (m_user_domains && !RayOBBtest(p, from, m_Z))
                continue;

            // Cast ray into collision system
            GetSystem()->GetCollisionSystem()->RayHit(from, to, mrayhit_result);
            num_ray_casts++;

            if (mrayhit_result.hit) {
    #pragma omp critical(SCM_ray_casting)
                {
                    // If this is the first hit from this node, initialize the node record
                    if (m_grid_map.find(ij) == m_grid_map.end()) {
                        m_grid_map.insert(std::make_pair(ij, NodeRecord(z, z, GetInitNormal(ij))));
                    }

                    // Add to our map of hits to process
                    HitRecord record = {mrayhit_result.hitModel->GetContactable(), mrayhit_result.abs_hitPoint, -1};
                    hits.insert(std::make_pair(ij, record));
                    m_num_ray_hits++;
                }
            }
        }
        m_num_ray_casts += num_ray_casts;
    }

#else

    // Map-reduce approach (to eliminate critical section)

    const int nthreads = GetSystem()->GetNumThreadsChrono();
    std::vector<std::unordered_map<ChVector2i, HitRecord, CoordHash> > t_hits(nthreads);

    // Loop through all active domains (user-defined or default one)
    for (auto& p : m_active_domains) 
    {
        m_timer_ray_testing.start();

        // Loop through all vertices in the patch range
        int num_ray_casts = 0;
    #pragma omp parallel for num_threads(nthreads) reduction(+ : num_ray_casts)
        for (int k = 0; k < p.m_range.size(); k++) {
            int t_num = ChOMP::GetThreadNum();
            ChVector2i ij = p.m_range[k];

            // Move from (i, j) to (x, y, z) representation in the world frame
            double x = ij.x() * m_delta;
            double y = ij.y() * m_delta;
            double z = GetHeight(ij);

            // If enabled, check if current grid node in user-specified boundary
            if (m_boundary) {
                if (x > m_aabb.max.x() || x < m_aabb.min.x() || y > m_aabb.max.y() || y < m_aabb.min.y())
                    continue;
            }

            ChVector3d vertex_abs = m_frame.TransformPointLocalToParent(ChVector3d(x, y, z));

            // Create ray at current grid location
            ChCollisionSystem::ChRayhitResult mrayhit_result;
            ChVector3d to = vertex_abs + m_Z * m_test_offset_up;
            ChVector3d from = to - m_Z * m_test_offset_down;

            // Ray-OBB test (quick rejection)
            if (m_user_domains && !RayOBBtest(p, from, m_Z))
                continue;

            // Cast ray into collision system
            GetSystem()->GetCollisionSystem()->RayHit(from, to, mrayhit_result);
            num_ray_casts++;

            if (mrayhit_result.hit) {
                // Add to our map of hits to process
                HitRecord record = {mrayhit_result.hitModel->GetContactable(), mrayhit_result.abs_hitPoint, -1};
                t_hits[t_num].insert(std::make_pair(ij, record));
            }
        }

        m_timer_ray_testing.stop();

        m_num_ray_casts += num_ray_casts;

        // Sequential insertion in global hits
        for (int t_num = 0; t_num < nthreads; t_num++) {
            for (auto& h : t_hits[t_num]) {
                // If this is the first hit from this node, initialize the node record
                if (m_grid_map.find(h.first) == m_grid_map.end()) {
                    double z = GetInitHeight(h.first);
                    m_grid_map.insert(std::make_pair(h.first, NodeRecord(z, z, GetInitNormal(h.first))));
                }
                ////hits.insert(h);
            }

            hits.insert(t_hits[t_num].begin(), t_hits[t_num].end());
            t_hits[t_num].clear();
        }
        m_num_ray_hits = (int)hits.size();
    }

#endif

    m_timer_ray_casting.stop();

    // --------------------
    // Find contact patches
    // --------------------

    m_timer_contact_patches.start();

    // Collect hit vertices assigned to each contact patch.
    struct ContactPatchRecord {
        std::vector<ChVector2d> points;  // points in contact patch (in reference plane)
        std::vector<ChVector2i> nodes;   // grid nodes in the contact patch
        double area;                     // contact patch area
        double perimeter;                // contact patch perimeter
        double oob;                      // approximate value of 1/b
    };
    std::vector<ContactPatchRecord> contact_patches;

    // Loop through all hit nodes and determine to which contact patch they belong.
    // Use a queue-based flood-filling algorithm based on the neighbors of each hit node.
    m_num_contact_patches = 0;
    for (auto& h : hits) {
        if (h.second.patch_id != -1)
            continue;

        ChVector2i ij = h.first;

        // Make a new contact patch and add this hit node to it
        h.second.patch_id = m_num_contact_patches++;
        ContactPatchRecord patch;
        patch.nodes.push_back(ij);
        patch.points.push_back(ChVector2d(m_delta * ij.x(), m_delta * ij.y()));

        // Add current node to the work queue
        std::queue<ChVector2i> todo;
        todo.push(ij);

        while (!todo.empty()) {
            auto crt = hits.find(todo.front());  // Current hit node is first element in queue
            todo.pop();                          // Remove first element from queue

            ChVector2i crt_ij = crt->first;
            int crt_patch = crt->second.patch_id;

            // Loop through the neighbors of the current hit node
            for (int k = 0; k < 4; k++) {
                ChVector2i nbr_ij = crt_ij + neighbors4[k];
                // If neighbor is not a hit node, move on
                auto nbr = hits.find(nbr_ij);
                if (nbr == hits.end())
                    continue;
                // If neighbor already assigned to a contact patch, move on
                if (nbr->second.patch_id != -1)
                    continue;
                // Assign neighbor to the same contact patch
                nbr->second.patch_id = crt_patch;
                // Add neighbor point to patch lists
                patch.nodes.push_back(nbr_ij);
                patch.points.push_back(ChVector2d(m_delta * nbr_ij.x(), m_delta * nbr_ij.y()));
                // Add neighbor to end of work queue
                todo.push(nbr_ij);
            }
        }
        contact_patches.push_back(patch);
    }

    // Calculate area and perimeter of each contact patch.
    // Calculate approximation to Beker term 1/b.
    for (auto& p : contact_patches) {
        utils::ChConvexHull2D ch(p.points);
        p.area = ch.GetArea();
        p.perimeter = ch.GetPerimeter();
        if (p.area < 1e-6) {
            p.oob = 0;
        } else {
            p.oob = p.perimeter / (2 * p.area);
        }
    }

    m_timer_contact_patches.stop();

    // ----------------------
    // Compute contact forces
    // ----------------------

    m_timer_contact_forces.start();

    // Initialize local values for the soil parameters
    double Bekker_Kphi = m_Bekker_Kphi;
    double Bekker_Kc = m_Bekker_Kc;
    double Bekker_n = m_Bekker_n;
    double Mohr_cohesion = m_Mohr_cohesion;
    double Mohr_mu = m_Mohr_mu;
    double Janosi_shear = m_Janosi_shear;
    double elastic_K = m_elastic_K;
    double damping_R = m_damping_R;

    // Process only hit nodes
    for (auto& h : hits) {
        ChVector2d ij = h.first;

        auto& nr = m_grid_map.at(ij);      // node record
        const double& ca = nr.normal.z();  // cosine of angle between local normal and SCM plane vertical

        ChContactable* contactable = h.second.contactable;
        const ChVector3d& hit_point_abs = h.second.abs_point;
        int patch_id = h.second.patch_id;

        auto hit_point_loc = m_frame.TransformPointParentToLocal(hit_point_abs);

        if (m_soil_fun) {
            double Mohr_friction;
            m_soil_fun->Set(hit_point_loc, Bekker_Kphi, Bekker_Kc, Bekker_n, Mohr_cohesion, Mohr_friction, Janosi_shear,
                            elastic_K, damping_R);
            Mohr_mu = std::tan(Mohr_friction * CH_DEG_TO_RAD);
        }

        nr.hit_level = hit_point_loc.z();                              // along SCM z axis
        double p_hit_offset = ca * (nr.level_initial - nr.hit_level);  // along local normal direction

        // Elastic try (along local normal direction)
        nr.sigma = elastic_K * (p_hit_offset - nr.sinkage_plastic);

        // Handle unilaterality
        if (nr.sigma < 0) {
            nr.sigma = 0;
            continue;
        }

        // Mark current node as modified
        m_modified_nodes.push_back(ij);

        // Calculate velocity at touched grid node
        ChVector3d point_local(ij.x() * m_delta, ij.y() * m_delta, nr.level);
        ChVector3d point_abs = m_frame.TransformPointLocalToParent(point_local);
        ChVector3d speed_abs = contactable->GetContactPointSpeed(point_abs);

        // Calculate normal and tangent directions (expressed in absolute frame)
        ChVector3d N = m_frame.TransformDirectionLocalToParent(nr.normal);
        double Vn = Vdot(speed_abs, N);
        ChVector3d T = -(speed_abs - Vn * N);
        T.Normalize();

        // Update total sinkage and current level for this hit node
        nr.sinkage = p_hit_offset;
        nr.level = nr.hit_level;

        // Accumulate shear for Janosi-Hanamoto (along local tangent direction)
        nr.kshear += Vdot(speed_abs, -T) * GetSystem()->GetStep();

        // Plastic correction (along local normal direction)
        if (nr.sigma > nr.sigma_yield) {
            // Bekker formula
            nr.sigma = (contact_patches[patch_id].oob * Bekker_Kc + Bekker_Kphi) * std::pow(nr.sinkage, Bekker_n);
            nr.sigma_yield = nr.sigma;
            double old_sinkage_plastic = nr.sinkage_plastic;
            nr.sinkage_plastic = nr.sinkage - nr.sigma / elastic_K;
            nr.step_plastic_flow = (nr.sinkage_plastic - old_sinkage_plastic) / GetSystem()->GetStep();
        }

        // Elastic sinkage (along local normal direction)
        nr.sinkage_elastic = nr.sinkage - nr.sinkage_plastic;

        // Add compressive speed-proportional damping (not clamped by pressure yield)
        ////if (Vn < 0) {
        nr.sigma += -Vn * damping_R;
        ////}

        // Mohr-Coulomb
        double tau_max = Mohr_cohesion + nr.sigma * Mohr_mu;

        // Janosi-Hanamoto (along local tangent direction)
        nr.tau = tau_max * (1.0 - std::exp(-(nr.kshear / Janosi_shear)));

        // Calculate normal and tangential forces (in local node directions).
        // If specified, combine properties for soil-contactable interaction and soil-soil interaction.
        ChVector3d Fn = N * m_area * nr.sigma;
        ChVector3d Ft;

        //// TODO:  take into account "tread height" (add to SCMContactableData)?

        if (auto cprops = contactable->GetUserData<vehicle::SCMContactableData>()) {
            // Use weighted sum of soil-contactable and soil-soil parameters
            double c_tau_max = cprops->Mohr_cohesion + nr.sigma * cprops->Mohr_mu;
            double c_tau = c_tau_max * (1.0 - std::exp(-(nr.kshear / cprops->Janosi_shear)));
            double ratio = cprops->area_ratio;
            Ft = T * m_area * ((1 - ratio) * nr.tau + ratio * c_tau);
        } else {
            // Use only soil-soil parameters
            Ft = T * m_area * nr.tau;
        }

        if (ChBody* body = dynamic_cast<ChBody*>(contactable)) {
            // Accumulate resultant force and torque (expressed in global frame) for this rigid body.
            // The resultant force is assumed to be applied at the body COM.
            ChVector3d force = Fn + Ft;
            ChVector3d moment = Vcross(point_abs - body->GetPos(), force);

            auto itr = m_body_forces.find(body);
            if (itr == m_body_forces.end()) {
                // Create new entry and initialize generalized force
                auto frc = std::make_pair(force, moment);
                m_body_forces.insert(std::make_pair(body, frc));
            } else {
                // Update generalized force
                itr->second.first += force;
                itr->second.second += moment;
            }
        } else if (fea::ChContactTriangleXYZ* tri = dynamic_cast<fea::ChContactTriangleXYZ*>(contactable)) {
            // Accumulate forces (expressed in global frame) for the nodes of this contact triangle.
            ChVector3d force = Fn + Ft;

            double s[3];
            tri->ComputeUVfromP(point_abs, s[1], s[2]);
            s[0] = 1 - s[1] - s[2];

            for (int i = 0; i < 3; i++) {
                auto node = tri->GetNode(i);
                auto node_force = s[i] * force;
                auto itr = m_node_forces.find(node);
                if (itr == m_node_forces.end()) {
                    // Create new entry and initialize force
                    m_node_forces.insert(std::make_pair(node, node_force));
                } else {
                    // Update force
                    itr->second += node_force;
                }
            }
        } else if (ChLoadableUV* surf = dynamic_cast<ChLoadableUV*>(contactable)) {
            if (!m_cosim_mode) {
                // [](){} Trick: no deletion for this shared ptr
                std::shared_ptr<ChLoadableUV> ssurf(surf, [](ChLoadableUV*) {});
                auto loader = chrono_types::make_shared<ChLoaderForceOnSurface>(ssurf);
                loader->SetForce(Fn + Ft);
                loader->SetApplication(0.5, 0.5);  //// TODO set UV, now just in middle
                auto load = chrono_types::make_shared<ChLoad>(loader);
                this->Add(load);
            }

            // Accumulate contact forces for this surface.
            //// TODO
        }

        // Update grid node height (in local SCM frame, along SCM z axis)
        nr.level = nr.level_initial - nr.sinkage / ca;

    }  // end loop on ray hits

    // Create loads for bodies and nodes to apply the accumulated terrain force/torque for each of them
    if (!m_cosim_mode) {
        for (const auto& f : m_body_forces) {
            std::shared_ptr<ChBody> sbody(f.first, [](ChBody*) {});
            auto force_load =
                chrono_types::make_shared<ChLoadBodyForce>(sbody, f.second.first, false, sbody->GetPos(), false);
            auto torque_load = chrono_types::make_shared<ChLoadBodyTorque>(sbody, f.second.second, false);
            Add(force_load);
            Add(torque_load);
        }

        for (const auto& f : m_node_forces) {
            auto force_load = chrono_types::make_shared<ChLoadNodeXYZ>(f.first, f.second);
            Add(force_load);
        }
    }

    m_timer_contact_forces.stop();

    // --------------------------------------------------
    // Flow material to the side of rut, using heuristics
    // --------------------------------------------------

    m_timer_bulldozing.start();

    m_num_erosion_nodes = 0;

    if (m_bulldozing) {
        typedef std::unordered_set<ChVector2i, CoordHash> NodeSet;

        // Maximum level change between neighboring nodes (smoothing phase)
        double dy_lim = m_delta * m_erosion_slope;

        // (1) Raise boundaries of each contact patch
        m_timer_bulldozing_boundary.start();

        NodeSet boundary;  // union of contact patch boundaries
        for (auto p : contact_patches) {
            NodeSet p_boundary;  // boundary of effective contact patch

            // Calculate the displaced material from all touched nodes and identify boundary
            double tot_step_flow = 0;
            for (const auto& ij : p.nodes) {                 // for each node in contact patch
                const auto& nr = m_grid_map.at(ij);          //   get node record
                if (nr.sigma <= 0)                           //   if node not touched
                    continue;                                //     skip (not in effective patch)
                tot_step_flow += nr.step_plastic_flow;       //   accumulate displaced material
                for (int k = 0; k < 4; k++) {                //   check each node neighbor
                    ChVector2i nbr_ij = ij + neighbors4[k];  //     neighbor node coordinates
                    ////if (!CheckMeshBounds(nbr_ij))                     //     if neighbor out of bounds
                    ////    continue;                                     //       skip neighbor
                    if (m_grid_map.find(nbr_ij) == m_grid_map.end())  //     if neighbor not yet recorded
                        p_boundary.insert(nbr_ij);                    //       set neighbor as boundary
                    else if (m_grid_map.at(nbr_ij).sigma <= 0)        //     if neighbor not touched
                        p_boundary.insert(nbr_ij);                    //       set neighbor as boundary
                }
            }
            tot_step_flow *= GetSystem()->GetStep();

            // Target raise amount for each boundary node (unless clamped)
            double diff = m_flow_factor * tot_step_flow / p_boundary.size();

            // Raise boundary (create a sharp spike which will be later smoothed out with erosion)
            for (const auto& ij : p_boundary) {                                  // for each node in bndry
                m_modified_nodes.push_back(ij);                                  //   mark as modified
                if (m_grid_map.find(ij) == m_grid_map.end()) {                   //   if not yet recorded
                    double z = GetInitHeight(ij);                                //     undeformed height
                    const ChVector3d& n = GetInitNormal(ij);                     //     terrain normal
                    m_grid_map.insert(std::make_pair(ij, NodeRecord(z, z, n)));  //     add new node record
                    m_modified_nodes.push_back(ij);                              //     mark as modified
                }                                                                //
                auto& nr = m_grid_map.at(ij);                                    //   node record
                nr.erosion = true;                                               //   add to erosion domain
                AddMaterialToNode(diff, nr);                                     //   add raise amount
            }

            // Accumulate boundary
            boundary.insert(p_boundary.begin(), p_boundary.end());

        }  // end for contact_patches

        m_timer_bulldozing_boundary.stop();

        // (2) Calculate erosion domain (dilate boundary)
        m_timer_bulldozing_domain.start();

        NodeSet erosion_domain = boundary;
        NodeSet erosion_front = boundary;  // initialize erosion front to boundary nodes
        for (int i = 0; i < m_erosion_propagations; i++) {
            NodeSet front;                                   // new erosion front
            for (const auto& ij : erosion_front) {           // for each node in current erosion front
                for (int k = 0; k < 4; k++) {                // check each of its neighbors
                    ChVector2i nbr_ij = ij + neighbors4[k];  //   neighbor node coordinates
                    ////if (!CheckMeshBounds(nbr_ij))                       //   if out of bounds
                    ////    continue;                                       //     ignore neighbor
                    if (m_grid_map.find(nbr_ij) == m_grid_map.end()) {  //   if neighbor not yet recorded
                        double z = GetInitHeight(nbr_ij);               //     undeformed height at neighbor location
                        const ChVector3d& n = GetInitNormal(nbr_ij);    //     terrain normal at neighbor location
                        NodeRecord nr(z, z, n);                         //     create new record
                        nr.erosion = true;                              //     include in erosion domain
                        m_grid_map.insert(std::make_pair(nbr_ij, nr));  //     add new node record
                        front.insert(nbr_ij);                           //     add neighbor to new front
                        m_modified_nodes.push_back(nbr_ij);             //     mark as modified
                    } else {                                            //   if neighbor previously recorded
                        NodeRecord& nr = m_grid_map.at(nbr_ij);         //     get existing record
                        if (!nr.erosion && nr.sigma <= 0) {             //     if neighbor not touched
                            nr.erosion = true;                          //       include in erosion domain
                            front.insert(nbr_ij);                       //       add neighbor to new front
                            m_modified_nodes.push_back(nbr_ij);         //       mark as modified
                        }
                    }
                }
            }
            erosion_domain.insert(front.begin(), front.end());  // add current front to erosion domain
            erosion_front = front;                              // advance erosion front
        }

        m_num_erosion_nodes = static_cast<int>(erosion_domain.size());
        m_timer_bulldozing_domain.stop();

        // (3) Erosion algorithm on domain
        m_timer_bulldozing_erosion.start();

        for (int iter = 0; iter < m_erosion_iterations; iter++) {
            for (const auto& ij : erosion_domain) {
                auto& nr = m_grid_map.at(ij);
                for (int k = 0; k < 4; k++) {
                    ChVector2i nbr_ij = ij + neighbors4[k];
                    auto rec = m_grid_map.find(nbr_ij);
                    if (rec == m_grid_map.end())
                        continue;
                    auto& nbr_nr = rec->second;

                    // (3.1) Flow remaining material to neighbor
                    double diff = 0.5 * (nr.massremainder - nbr_nr.massremainder) / 4;  //// TODO: rethink this!
                    if (diff > 0) {
                        RemoveMaterialFromNode(diff, nr);
                        AddMaterialToNode(diff, nbr_nr);
                    }

                    // (3.2) Smoothing
                    if (nbr_nr.sigma == 0) {
                        double dy = (nr.level + nr.massremainder) - (nbr_nr.level + nbr_nr.massremainder);
                        diff = 0.5 * (std::abs(dy) - dy_lim) / 4;  //// TODO: rethink this!
                        if (diff > 0) {
                            if (dy > 0) {
                                RemoveMaterialFromNode(diff, nr);
                                AddMaterialToNode(diff, nbr_nr);
                            } else {
                                RemoveMaterialFromNode(diff, nbr_nr);
                                AddMaterialToNode(diff, nr);
                            }
                        }
                    }
                }
            }
        }

        m_timer_bulldozing_erosion.stop();

    }  // end do_bulldozing

    m_timer_bulldozing.stop();

    // --------------------
    // Update visualization
    // --------------------

    m_timer_visualization.start();

    if (m_trimesh_shape) {
        // Loop over list of modified nodes and adjust corresponding mesh vertices.
        // If not rendering a wireframe mesh, also update normals.
        for (const auto& ij : m_modified_nodes) {
            if (!CheckMeshBounds(ij))                 // if node outside mesh
                continue;                             //   do nothing
            const auto& nr = m_grid_map.at(ij);       // grid node record
            int iv = GetMeshVertexIndex(ij);          // mesh vertex index
            UpdateMeshVertexCoordinates(ij, iv, nr);  // update vertex coordinates and color
            modified_vertices.push_back(iv);          // cache in list of modified mesh vertices
            if (!m_trimesh_shape->IsWireframe())      // if not wireframe
                UpdateMeshVertexNormal(ij, iv);       // update vertex normal
        }

        m_trimesh_shape->SetModifiedVertices(modified_vertices);
    }

    m_timer_visualization.stop();
}

void SCMLoaderOld::AddMaterialToNode(double amount, NodeRecord& nr) {
    if (amount > nr.hit_level - nr.level) {                      //   if not possible to assign all mass
        nr.massremainder += amount - (nr.hit_level - nr.level);  //     material to be further propagated
        amount = nr.hit_level - nr.level;                        //     clamp raise amount
    }                                                            //
    nr.level += amount;                                          //   modify node level
    nr.level_initial += amount;                                  //   reset node initial level
}

void SCMLoaderOld::RemoveMaterialFromNode(double amount, NodeRecord& nr) {
    if (nr.massremainder > amount) {                                 // if too much remainder material
        nr.massremainder -= amount;                                  //   decrease remainder material
        /*amount = 0;*/                                              //   ???
    } else if (nr.massremainder < amount && nr.massremainder > 0) {  // if not enough remainder material
        amount -= nr.massremainder;                                  //   clamp removed amount
        nr.massremainder = 0;                                        //   remainder material exhausted
    }                                                                //
    nr.level -= amount;                                              //   modify node level
    nr.level_initial -= amount;                                      //   reset node initial level
}

// Update vertex position and color in visualization mesh
void SCMLoaderOld::UpdateMeshVertexCoordinates(const ChVector2i ij, int iv, const NodeRecord& nr) {
    auto& trimesh = *m_trimesh_shape->GetMesh();
    std::vector<ChVector3d>& vertices = trimesh.GetCoordsVertices();
    std::vector<ChColor>& colors = trimesh.GetCoordsColors();

    // Update visualization mesh vertex position
    vertices[iv] = m_frame.TransformPointLocalToParent(ChVector3d(ij.x() * m_delta, ij.y() * m_delta, nr.level));

    // Update visualization mesh vertex color
    if (m_plot_type != SCMTerrainOld::PLOT_NONE) {
        ChColor color;
        switch (m_plot_type) {
            case SCMTerrainOld::PLOT_LEVEL:
                color = m_colormap->Get(nr.level, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_LEVEL_INITIAL:
                color = m_colormap->Get(nr.level_initial, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_SINKAGE:
                color = m_colormap->Get(nr.sinkage, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_SINKAGE_ELASTIC:
                color = m_colormap->Get(nr.sinkage_elastic, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_SINKAGE_PLASTIC:
                color = m_colormap->Get(nr.sinkage_plastic, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_STEP_PLASTIC_FLOW:
                color = m_colormap->Get(nr.step_plastic_flow, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_K_JANOSI:
                color = m_colormap->Get(nr.kshear, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_PRESSURE:
                color = m_colormap->Get(nr.sigma, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_PRESSURE_YIELD:
                color = m_colormap->Get(nr.sigma_yield, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_SHEAR:
                color = m_colormap->Get(nr.tau, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_MASSREMAINDER:
                color = m_colormap->Get(nr.massremainder, m_plot_v_min, m_plot_v_max);
                break;
            case SCMTerrainOld::PLOT_ISLAND_ID:
                if (nr.erosion)
                    color = ChColor(0, 0, 0);
                if (nr.sigma > 0)
                    color = ChColor(1, 0, 0);
                break;
            case SCMTerrainOld::PLOT_IS_TOUCHED:
                if (nr.sigma > 0)
                    color = ChColor(1, 0, 0);
                else
                    color = ChColor(0, 0, 1);
                break;
            case SCMTerrainOld::PLOT_NONE:
                break;
        }
        colors[iv] = color;
    }
}

// Update vertex normal in visualization mesh.
void SCMLoaderOld::UpdateMeshVertexNormal(const ChVector2i ij, int iv) {
    auto& trimesh = *m_trimesh_shape->GetMesh();
    std::vector<ChVector3d>& vertices = trimesh.GetCoordsVertices();
    std::vector<ChVector3d>& normals = trimesh.GetCoordsNormals();
    std::vector<ChVector3i>& idx_normals = trimesh.GetIndicesNormals();

    // Average normals from adjacent faces
    normals[iv] = ChVector3d(0, 0, 0);
    auto faces = GetMeshFaceIndices(ij);
    for (auto f : faces) {
        ChVector3d nrm = Vcross(vertices[idx_normals[f][1]] - vertices[idx_normals[f][0]],
                                vertices[idx_normals[f][2]] - vertices[idx_normals[f][0]]);
        nrm.Normalize();
        normals[iv] += nrm;
    }
    normals[iv] /= (double)faces.size();
}

// Get the heights of modified grid nodes.
std::vector<SCMTerrainOld::NodeLevel> SCMLoaderOld::GetModifiedNodes(bool all_nodes) const {
    std::vector<SCMTerrainOld::NodeLevel> nodes;
    if (all_nodes) {
        for (const auto& nr : m_grid_map) {
            nodes.push_back(std::make_pair(nr.first, nr.second.level));
        }
    } else {
        for (const auto& ij : m_modified_nodes) {
            auto rec = m_grid_map.find(ij);
            assert(rec != m_grid_map.end());
            nodes.push_back(std::make_pair(ij, rec->second.level));
        }
    }
    return nodes;
}

// Modify the level of grid nodes from the given list.
// NOTE: We set only the level of the specified nodes and none of the other soil properties.
//       As such, some plot types may be incorrect at these nodes.
void SCMLoaderOld::SetModifiedNodes(const std::vector<SCMTerrain::NodeLevel>& nodes) {
    for (const auto& n : nodes) {
        // Modify existing entry in grid map or insert new one
        m_grid_map[n.first] = SCMLoaderOld::NodeRecord(n.second, n.second, GetInitNormal(n.first));
    }

    // Update visualization
    if (m_trimesh_shape) {
        for (const auto& n : nodes) {
            auto ij = n.first;                           // grid location
            if (!CheckMeshBounds(ij))                    // if outside mesh
                continue;                                //   do nothing
            const auto& nr = m_grid_map.at(ij);          // grid node record
            int iv = GetMeshVertexIndex(ij);             // mesh vertex index
            UpdateMeshVertexCoordinates(ij, iv, nr);     // update vertex coordinates and color
            if (!m_trimesh_shape->IsWireframe())         // if not in wireframe mode
                UpdateMeshVertexNormal(ij, iv);          //   update vertex normal
            m_external_modified_vertices.push_back(iv);  // cache in list
        }
    }
}

}  // end namespace vehicle
}  // end namespace chrono
