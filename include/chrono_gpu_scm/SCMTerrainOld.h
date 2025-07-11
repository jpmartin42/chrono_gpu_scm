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

// CHRONO_GPU_SCM NOTE: This is a copy of the 9.0 SCM code used to confirm file and linking structure.
// Delete this once GPU is up and running

#ifndef SCM_TERRAIN_OLD_H
#define SCM_TERRAIN_OLD_H

#include <string>
#include <ostream>
#include <unordered_map>

#include "chrono/core/ChTimer.h"
#include "chrono/assets/ChVisualShapeTriangleMesh.h"
#include "chrono/assets/ChColormap.h"
#include "chrono/physics/ChBody.h"
#include "chrono/physics/ChLoadContainer.h"
#include "chrono/physics/ChLoadsBody.h"
#include "chrono/physics/ChLoadsNodeXYZ.h"
#include "chrono/physics/ChSystem.h"
#include "chrono/fea/ChNodeFEAxyz.h"

#include "chrono_vehicle/ChApiVehicle.h"
#include "chrono_vehicle/ChSubsysDefs.h"
#include "chrono_vehicle/ChTerrain.h"
#include "chrono_vehicle/ChWorldFrame.h"

namespace chrono {
namespace vehicle {

class SCMLoaderOld;

/// @addtogroup vehicle_terrain
/// @{

/// Deformable terrain model.
/// This class implements a deformable terrain based on the Soil Contact Model.
/// Unlike RigidTerrain, the vertical coordinates of this terrain mesh can be deformed
/// due to interaction with ground vehicles or other collision shapes.
class CH_VEHICLE_API SCMTerrainOld : public ChTerrain {
  public:
    enum DataPlotType {
        PLOT_NONE,
        PLOT_LEVEL,
        PLOT_LEVEL_INITIAL,
        PLOT_SINKAGE,
        PLOT_SINKAGE_ELASTIC,
        PLOT_SINKAGE_PLASTIC,
        PLOT_STEP_PLASTIC_FLOW,
        PLOT_PRESSURE,
        PLOT_PRESSURE_YIELD,
        PLOT_SHEAR,
        PLOT_K_JANOSI,
        PLOT_IS_TOUCHED,
        PLOT_ISLAND_ID,
        PLOT_MASSREMAINDER
    };

    /// Information at SCM node.
    struct NodeInfo {
        double sinkage;          ///< sinkage, along local normal direction
        double sinkage_plastic;  ///< sinkage due to plastic deformation, along local normal direction
        double sinkage_elastic;  ///< sinkage due to plastic deformation, along local normal direction
        double sigma;            ///< normal pressure, along local normal direction
        double sigma_yield;      ///< yield pressure, along local normal direction
        double kshear;           ///< Janosi-Hanamoto shear, along local tangent direction
        double tau;              ///< shear stress, along local tangent direction

        NodeInfo() = default;
    };

    /// Construct a default SCM deformable terrain.
    /// The user is responsible for calling various Set methods before Initialize.
    SCMTerrainOld(ChSystem* system,               ///< [in] containing multibody system
               bool visualization_mesh = true  ///< [in] enable/disable visualization asset
    );

    ~SCMTerrainOld() {}

    /// Set the SCM reference frame.
    /// By default, the reference frame is aligned with the global ISO vehicle reference frame.
    /// To set as Y up, call SetReferenceFrame(ChCoordys(VNULL, QuatFromAngleX(-CH_PI_2)));
    void SetReferenceFrame(const ChCoordsys<>& plane);

    /// Get the current SCM reference frame.
    /// The SCM terrain patch is defined relative to the (x,y) plane of this frame, with normal along the Z axis.
    const ChCoordsys<>& GetReferenceFrame() const;

    /// Set the properties of the SCM soil model.
    /// These parameters are described in: "Parameter Identification of a Planetary Rover Wheel-Soil Contact Model via a
    /// Bayesian Approach", A.Gallina, R. Krenn et al. Note that the original SCM model does not include the K and R
    /// coefficients. A very large value of K and R=0 reproduce the original SCM.
    void SetSoilParameters(
        double Bekker_Kphi,    ///< Kphi, frictional modulus in Bekker model
        double Bekker_Kc,      ///< Kc, cohesive modulus in Bekker model
        double Bekker_n,       ///< n, exponent of sinkage in Bekker model (usually 0.6...1.8)
        double Mohr_cohesion,  ///< Cohesion for shear failure [Pa]
        double Mohr_friction,  ///< Friction angle for shear failure [degree]
        double Janosi_shear,   ///< Shear parameter in Janosi-Hanamoto formula [m]
        double elastic_K,      ///< elastic stiffness K per unit area, [Pa/m] (must be larger than Kphi)
        double damping_R       ///< vertical damping R per unit area [Pa.s/m] (proportional to vertical speed)
    );

    /// Enable/disable the creation of soil inflation at the side of the ruts (bulldozing effects).
    void EnableBulldozing(bool mb);

    /// Set parameters controlling the creation of side ruts (bulldozing effects).
    void SetBulldozingParameters(
        double erosion_angle,          ///< angle of erosion of the displaced material [degrees]
        double flow_factor = 1.0,      ///< growth of lateral volume relative to pressed volume
        int erosion_iterations = 3,    ///< number of erosion refinements per timestep
        int erosion_propagations = 10  ///< number of concentric vertex selections subject to erosion
    );

    /// Set the vertical level up to which collision is tested (relative to the reference level at the sample point).
    /// Since the contact is unilateral, this could be zero. However, when computing bulldozing flow, one might also
    /// need to know if in the surrounding there is some potential future contact: so it might be better to use a
    /// positive value (but not higher than the max. expected height of the bulldozed rubble, to avoid slowdown of
    /// collision tests). Default: 0.1 m.
    void SetTestHeight(double offset);

    ///  Return the current test height level.
    double GetTestHeight() const;

    /// Set the color plot type for the SCM mesh.
    /// Specify the minimum and maximum values for false coloring.
    void SetPlotType(DataPlotType plot_type, double min_val, double max_val);

    /// Set the colormap type for false coloring of the SCM mesh.
    /// The default colormap is JET (a divergent blue-red map).
    void SetColormap(ChColormap::Type type);

    /// Get the type of the colormap currently in use.
    ChColormap::Type GetColormapType() const;

    /// Get the colormap object in current use.
    const ChColormap& GetColormap() const;

    /// Set visualization color.
    void SetColor(const ChColor& color);

    /// Set texture properties.
    void SetTexture(const std::string tex_file,  ///< [in] texture filename
                    float scale_x = 1,           ///< [in] texture X scale
                    float scale_y = 1            ///< [in] texture Y scale
    );

    /// Set boundary of the SCM computational domain.
    /// By default, the SCM terrain patch extends to infinity in the x-y plane, beyond the area used to initialize it;
    /// outside the initialization area, the height of the SCM terrain is that of the closest initialized point.
    /// By specifying a boundary, SCM terrain forces outside that boundary are not generated. This feature is useful in
    /// stitching an environment with multiple SCM terrain patches or with a combination of SCM and rigid terrain
    /// patches. The boundary is specified as an axis-aligned bounding box expressed relative to the SCM reference
    /// plane. Note that the z values of the provided AABB are not used (as long as the AABB is not inverted).
    void SetBoundary(const ChAABB& aabb);

    /// Add a new moving active domain associated with the specified body.
    /// Note: the OOBB is placed relative to the body *reference frame*.
    /// Multiple calls to this function can be made, each of them adding a new active active domain.
    /// The union of all currently defined active domains is used to reduce the number of ray casting operations, by
    /// ensuring that rays are generated only from SCM grid nodes inside the projection of the an actiove domains's OOBB
    /// onto the SCM reference plane. If there are no user-provided active domains, a single default one is defined to
    /// encompass all collision shapes in the system at any given time.
    void AddActiveDomain(std::shared_ptr<ChBody> body,   ///< [in] monitored body
                         const ChVector3d& OOBB_center,  ///< [in] OOBB center, relative to body reference frame
                         const ChVector3d& OOBB_dims     ///< [in] OOBB dimensions
    );

    /// Class to be used as a callback interface for location-dependent soil parameters.
    /// A derived class must implement Set() and set *all* soil parameters (no defaults are provided).
    class CH_VEHICLE_API SoilParametersCallback {
      public:
        virtual ~SoilParametersCallback() {}

        /// Set the soil properties at a given (x,y) location (below the given point).
        /// Attention: the location is assumed to be provided in the SCM reference frame!
        virtual void Set(
            const ChVector3d& loc,  ///< query location
            double& Bekker_Kphi,    ///< frictional modulus in Bekker model
            double& Bekker_Kc,      ///< cohesive modulus in Bekker model
            double& Bekker_n,       ///< exponent of sinkage in Bekker model (usually 0.6...1.8)
            double& Mohr_cohesion,  ///< cohesion for shear failure [Pa]
            double& Mohr_friction,  ///< friction angle for shear failure [degree]
            double& Janosi_shear,   ///< shear parameter in Janosi-Hanamoto formula [m]
            double& elastic_K,      ///< elastic stiffness K per unit area, [Pa/m] (must be larger than Kphi)
            double& damping_R       ///< vertical damping R per unit area [Pa.s/m] (proportional to vertical speed)
            ) = 0;
    };

    /// Specify the callback object to set the soil parameters at given (x,y) locations.
    /// To use constant soil parameters throughout the entire patch, use SetSoilParameters.
    void RegisterSoilParametersCallback(std::shared_ptr<SoilParametersCallback> cb);

    /// Get the initial (undeformed) terrain height below the specified location.
    double GetInitHeight(const ChVector3d& loc) const;

    /// Get the initial (undeformed) terrain normal at the point below the specified location.
    ChVector3d GetInitNormal(const ChVector3d& loc) const;

    /// Get the terrain height below the specified location.
    virtual double GetHeight(const ChVector3d& loc) const override;

    /// Get the terrain normal at the point below the specified location.
    virtual ChVector3d GetNormal(const ChVector3d& loc) const override;

    /// Get the terrain coefficient of friction at the point below the specified location.
    /// This coefficient of friction value may be used by certain tire models to modify
    /// the tire characteristics, but it will have no effect on the interaction of the terrain
    /// with other objects (including tire models that do not explicitly use it).
    /// For SCMTerrain, this function defers to the user-provided functor object
    /// of type ChTerrain::FrictionFunctor, if one was specified.
    /// Otherwise, it returns the constant value of 0.8.
    virtual float GetCoefficientFriction(const ChVector3d& loc) const override;

    /// Get SCM information at the node closest to the specified location.
    NodeInfo GetNodeInfo(const ChVector3d& loc) const;

    /// Get the visualization triangular mesh.
    std::shared_ptr<ChVisualShapeTriangleMesh> GetMesh() const;

    /// Set the visualization mesh as wireframe or as solid (default: wireframe).
    /// Note: in wireframe mode, normals for the visualization mesh are not calculated.
    void SetMeshWireframe(bool val);

    /// Save the visualization mesh as a Wavefront OBJ file.
    void WriteMesh(const std::string& filename) const;

    /// Enable/disable co-simulation mode (default: false).
    /// In co-simulation mode, the underlying SCM loader does not apply loads to interacting objects.
    /// Instead, contact forces are accumulated and available for extraction using GetContactForceBody and
    /// GetContactForceNode for rigid bodies and FEA nodes, respectively.
    void SetCosimulationMode(bool val);

    /// Initialize the terrain system (flat).
    /// This version creates a flat array of points.
    void Initialize(double sizeX,  ///< [in] terrain dimension in the X direction
                    double sizeY,  ///< [in] terrain dimension in the Y direction
                    double delta   ///< [in] grid spacing (may be slightly decreased)
    );

    /// Initialize the terrain system (height map).
    /// The initial undeformed terrain profile is provided via the specified image file as a height map.
    /// The terrain patch is scaled in the horizontal plane of the SCM frame to sizeX x sizeY, while the initial height
    /// is scaled between hMin and hMax (with the former corresponding to a pure balck pixel and the latter to a pure
    /// white pixel).  The SCM grid resolution is specified through 'delta' and initial heights at grid points are
    /// obtained through interpolation (outside the terrain patch, the SCM node height is initialized to the height of
    /// the closest image pixel). For visualization purposes, a triangular mesh is also generated from the provided
    /// image file.
    void Initialize(const std::string& heightmap_file,  ///< [in] filename for the height map (image file)
                    double sizeX,                       ///< [in] terrain dimension in the X direction
                    double sizeY,                       ///< [in] terrain dimension in the Y direction
                    double hMin,                        ///< [in] minimum height (black level)
                    double hMax,                        ///< [in] maximum height (white level)
                    double delta                        ///< [in] grid spacing (may be slightly decreased)
    );

    /// Initialize the terrain system (mesh).
    /// The initial undeformed terrain profile is provided via the specified Wavefront OBJ mesh file.
    /// The dimensions of the terrain patch in the horizontal plane of the SCM frame is set to the range of the x and y
    /// mesh vertex coordinates, respectively.  The SCM grid resolution is specified through 'delta' and initial heights
    /// at grid points are obtained through linear interpolation (outside the mesh footprint, the height of a grid node
    /// is set to the height of the closest point on the mesh).  A visualization mesh is created from the original mesh
    /// resampled at the grid node points.
    void Initialize(const std::string& mesh_file,  ///< [in] filename for the mesh (Wavefront OBJ)
                    double delta                   ///< [in] grid spacing (may be slightly decreased)
    );

    /// Initialize the terrain system (mesh).
    /// The initial undeformed terrain profile is provided via the specified triangular mesh.
    /// The dimensions of the terrain patch in the horizontal plane of the SCM frame is set to the range of the x and y
    /// mesh vertex coordinates, respectively.  The SCM grid resolution is specified through 'delta' and initial heights
    /// at grid points are obtained through linear interpolation (outside the mesh footprint, the height of a grid node
    /// is set to the height of the closest point on the mesh).  A visualization mesh is created from the original mesh
    /// resampled at the grid node points.
    void Initialize(const ChTriangleMeshConnected& trimesh,  ///< [in] surface triangular mesh
                    double delta                             ///< [in] grid spacing
    );

    /// Node height level at a given grid location.
    typedef std::pair<ChVector2i, double> NodeLevel;

    /// Get the heights of all modified grid nodes.
    /// If 'all_nodes = true', return modified nodes from the start of simulation.  Otherwise, return only the nodes
    /// modified over the last step.
    std::vector<NodeLevel> GetModifiedNodes(bool all_nodes = false) const;

    /// Modify the level of grid nodes from the given list.
    void SetModifiedNodes(const std::vector<NodeLevel>& nodes);

    /// Return the cummulative contact force on the specified body  (due to interaction with the SCM terrain).
    /// The return value is true if the specified body experiences contact forces and false otherwise.
    /// If contact forces are applied to the body, they are reduced to the body center of mass.
    bool GetContactForceBody(std::shared_ptr<ChBody> body, ChVector3d& force, ChVector3d& torque) const;

    /// Return the cummulative contact force on the specified mesh node (due to interaction with the SCM terrain).
    /// The return value is true if the specified node experiences contact forces and false otherwise.
    bool GetContactForceNode(std::shared_ptr<fea::ChNodeFEAxyz> node, ChVector3d& force) const;

    /// Return the number of rays cast at last step.
    int GetNumRayCasts() const;
    /// Return the number of ray hits at last step.
    int GetNumRayHits() const;
    /// Return the number of contact patches at last step.
    int GetNumContactPatches() const;
    /// Return the number of nodes in the erosion domain at last step (bulldosing effects).
    int GetNumErosionNodes() const;

    /// Return time for updating active domains at last step (ms).
    double GetTimerActiveDomains() const;
    /// Return time for geometric ray intersection tests at last step (ms).
    double GetTimerRayTesting() const;
    /// Return time for ray casting at last step (ms). Includes time for ray intersectin testing.
    double GetTimerRayCasting() const;
    /// Return time for computing contact patches at last step (ms).
    double GetTimerContactPatches() const;
    /// Return time for computing contact forces at last step (ms).
    double GetTimerContactForces() const;
    /// Return time for computing bulldozing effects at last step (ms).
    double GetTimerBulldozing() const;
    /// Return time for visualization assets update at last step (ms).
    double GetTimerVisUpdate() const;

    /// Print timing and counter information for last step.
    void PrintStepStatistics(std::ostream& os) const;

    std::shared_ptr<SCMLoaderOld> GetSCMLoader() const { return m_loader; }

    void SetBaseMeshLevel(double level);

  private:
    std::shared_ptr<SCMLoaderOld> m_loader;  ///< underlying load container for contact force generation

    friend class ChScmVisualizationVSG;
};

/// Parameters for soil-contactable interaction.
class CH_VEHICLE_API SCMContactableData {
  public:
    SCMContactableData(double area_ratio,     ///< area fraction with overriden parameters (in [0,1])
                       double Mohr_cohesion,  ///< cohesion for shear failure [Pa]
                       double Mohr_friction,  ///< friction angle for shear failure [degree]
                       double Janosi_shear    ///< shear parameter in Janosi-Hanamoto formula [m]
    );

  private:
    double area_ratio;     ///< fraction of contactable surface where soil-soil parameters are overriden
    double Mohr_cohesion;  ///< cohesion for shear failure [Pa]
    double Mohr_mu;        ///< coefficient of friction for shear failure [degree]
    double Janosi_shear;   ///< shear parameter in Janosi-Hanamoto formula [m]

    friend class SCMLoaderOld;
};

/// Underlying implementation of the Soil Contact Model.
class CH_VEHICLE_API SCMLoaderOld : public ChLoadContainer {
  public:
    SCMLoaderOld(ChSystem* system, bool visualization_mesh);
    ~SCMLoaderOld() {}

    /// Initialize the terrain system (flat).
    /// This version creates a flat array of points.
    void Initialize(double hsizeX,  ///< [in] terrain dimension in the X direction
                    double hsizeY,  ///< [in] terrain dimension in the Y direction
                    double delta    ///< [in] grid spacing (may be slightly decreased)
    );

    /// Initialize the terrain system (height map).
    /// The initial undeformed mesh is provided via the specified image file as a height map.
    void Initialize(const std::string& heightmap_file,  ///< [in] filename for the height map (image file)
                    double sizeX,                       ///< [in] terrain dimension in the X direction
                    double sizeY,                       ///< [in] terrain dimension in the Y direction
                    double hMin,                        ///< [in] minimum height (black level)
                    double hMax,                        ///< [in] maximum height (white level)
                    double delta                        ///< [in] grid spacing (may be slightly decreased)
    );

    /// Initialize the terrain system (mesh).
    /// The initial undeformed terrain profile is provided via the specified Wavefront OBJ mesh file.
    void Initialize(const std::string& mesh_file,  ///< [in] filename for the mesh (Wavefront OBJ)
                    double delta                   ///< [in] grid spacing (may be slightly decreased)
    );

    /// Initialize the terrain system (mesh).
    /// The initial undeformed terrain profile is provided via the specified triangular mesh.
    void Initialize(const ChTriangleMeshConnected& trimesh,  ///< [in] surface triangular mesh
                    double delta                             ///< [in] grid spacing
    );

  private:
    // SCM patch type.
    enum class PatchType {
        FLAT,        // flat patch
        HEIGHT_MAP,  // triangular mesh (generated from a gray-scale image height-map)
        TRI_MESH     // triangular mesh (provided through an OBJ file)
    };

    // Active domain parameters.
    struct ActiveDomainInfo {
        std::shared_ptr<ChBody> m_body;   // tracked body
        ChVector3d m_center;              // OOBB center, relative to body
        ChVector3d m_hdims;               // OOBB half-dimensions
        std::vector<ChVector2i> m_range;  // current grid nodes covered by the domain
        ChVector3d m_ooN;                 // current inverse of SCM normal in body frame
    };

    // Information at contacted node
    struct NodeRecord {
        double level_initial;      // initial node level (relative to SCM frame)
        double level;              // current node level (relative to SCM frame)
        double hit_level;          // ray hit level (relative to SCM frame)
        ChVector3d normal;         // normal of undeformed terrain (in SCM frame)
        double sinkage;            // along local normal direction
        double sinkage_plastic;    // along local normal direction
        double sinkage_elastic;    // along local normal direction
        double sigma;              // along local normal direction
        double sigma_yield;        // along local normal direction
        double kshear;             // along local tangent direction
        double tau;                // along local tangent direction
        bool erosion;              // for bulldozing
        double massremainder;      // for bulldozing
        double step_plastic_flow;  // for bulldozing

        NodeRecord() : NodeRecord(0, 0, ChVector3d(0, 0, 1)) {}
        ~NodeRecord() {}

        NodeRecord(double init_level, double level, const ChVector3d& n)
            : level_initial(init_level),
              level(level),
              hit_level(1e9),
              normal(n),
              sinkage(init_level - level),
              sinkage_plastic(0),
              sinkage_elastic(0),
              sigma(0),
              sigma_yield(0),
              kshear(0),
              tau(0),
              erosion(false),
              massremainder(0),
              step_plastic_flow(0) {}
    };

    // Hash function for a pair of integer grid coordinates
    struct CoordHash {
      public:
        // 31 is just a decently-sized prime number to reduce bucket collisions
        std::size_t operator()(const ChVector2i& p) const { return p.x() * 31 + p.y(); }
    };

    // Create visualization mesh
    void CreateVisualizationMesh(double sizeX, double sizeY);

    // Get the initial undeformed terrain height (relative to the SCM plane) at the specified grid node.
    double GetInitHeight(const ChVector2i& loc) const;

    // Get the initial undeformed terrain normal (relative to the SCM plane) at the specified grid node.
    ChVector3d GetInitNormal(const ChVector2i& loc) const;

    // Get the terrain height (relative to the SCM plane) at the specified grid node.
    double GetHeight(const ChVector2i& loc) const;

    // Get the terrain normal (relative to the SCM plane) at the specified grid vertex.
    ChVector3d GetNormal(const ChVector2d& loc) const;

    // Get the initial terrain height (expressed in World frame) below the specified location.
    double GetInitHeight(const ChVector3d& loc) const;

    // Get the initial terrain normal (expressed in World frame) at the point below the specified location.
    ChVector3d GetInitNormal(const ChVector3d& loc) const;

    // Get the terrain height (expressed in World frame) below the specified location.
    double GetHeight(const ChVector3d& loc) const;

    // Get the terrain normal (expressed in World frame) at the point below the specified location.
    ChVector3d GetNormal(const ChVector3d& loc) const;

    // Get index of trimesh vertex corresponding to the specified grid node.
    int GetMeshVertexIndex(const ChVector2i& loc);

    // Get indices of trimesh faces incident to the specified grid vertex.
    std::vector<int> GetMeshFaceIndices(const ChVector2i& loc);

    // Check if the provided grid location is within the visualization mesh bounds
    bool CheckMeshBounds(const ChVector2i& loc) const;

    // Return information at node closest to specified location.
    SCMTerrainOld::NodeInfo GetNodeInfo(const ChVector3d& loc) const;

    // Complete setup before first simulation step.
    virtual void SetupInitial() override;

    // Update the forces and the geometry, at the beginning of each timestep.
    virtual void Setup() override {
        ComputeInternalForces();
        ChLoadContainer::Update(ChTime, true);
    }

    virtual void Update(double time, bool update_assets) override {
        // Note!!! we cannot call ComputeInternalForces here, because Update() could
        // be called multiple times per timestep and not necessarily in time-increasing order;
        // this is a problem because in this force model the force is dissipative and keeps a 'history'.
        // Instead, we invoke ComputeInternalForces only at the beginning of the timestep in Setup().
        ChPhysicsItem::Update(time, update_assets);
    }

    // Synchronize information for a user-provided active domain.
    void UpdateActiveDomain(ActiveDomainInfo& ad, const ChVector3d& Z);

    // Synchronize information for the default active domain.
    void UpdateDefaultActiveDomain(ActiveDomainInfo& ad);

    // Ray-OBB intersection test
    bool RayOBBtest(const ActiveDomainInfo& ad, const ChVector3d& from, const ChVector3d& Z);

    // Reset the list of forces and fill it with forces from the soil contact model.
    // This is called automatically during timestepping (only at the beginning of each step).
    void ComputeInternalForces();

    // Override the ChLoadContainer method for computing the generalized force F term:
    virtual void IntLoadResidual_F(const unsigned int off,  // offset in R residual
                                   ChVectorDynamic<>& R,    // result: the R residual, R += c*F
                                   const double c           // a scaling factor
                                   ) override {
        ChLoadContainer::IntLoadResidual_F(off, R, c);
    }

    // Add specified amount of material (possibly clamped) to node.
    void AddMaterialToNode(double amount, NodeRecord& nr);

    // Remove specified amount of material (possibly clamped) from node.
    void RemoveMaterialFromNode(double amount, NodeRecord& nr);

    // Update vertex position and color in visualization mesh
    void UpdateMeshVertexCoordinates(const ChVector2i ij, int iv, const NodeRecord& nr);

    // Update vertex normal in visualization mesh
    void UpdateMeshVertexNormal(const ChVector2i ij, int iv);

    /// Get the heights of all modified grid nodes.
    /// If 'all_nodes = true', return modified nodes from the start of simulation.  Otherwise, return only the nodes
    /// modified over the last step.
    std::vector<SCMTerrainOld::NodeLevel> GetModifiedNodes(bool all_nodes = false) const;

    // Modify the level of grid nodes from the given list.
    void SetModifiedNodes(const std::vector<SCMTerrainOld::NodeLevel>& nodes);

    PatchType m_type;      ///< type of SCM patch
    ChCoordsys<> m_frame;  ///< SCM frame (deformation occurs along the z axis of this frame)
    ChVector3d m_Z;        ///< SCM plane vertical direction (in absolute frame)
    double m_delta;        ///< grid spacing
    double m_area;         ///< area of a grid cell
    int m_nx;              ///< range for grid indices in X direction: [-m_nx, +m_nx]
    int m_ny;              ///< range for grid indices in Y direction: [-m_ny, +m_ny]

    ChMatrixDynamic<> m_heights;  ///< (base) grid heights (when initializing from height-field map)
    double m_base_height;         ///< default height for vertices outside the projection of input mesh

    std::unordered_map<ChVector2i, NodeRecord, CoordHash> m_grid_map;  ///< modified grid nodes (persistent)
    std::vector<ChVector2i> m_modified_nodes;                          ///< modified grid nodes (current)

    ChAABB m_aabb;    ///< user-specified SCM terrain boundary
    bool m_boundary;  ///< user-specified SCM terrain boundary?

    std::vector<ActiveDomainInfo> m_active_domains;  ///< set of active domains
    bool m_user_domains;                             ///< user-specified active domains?

    double m_test_offset_down;  ///< offset for ray start
    double m_test_offset_up;    ///< offset for ray end

    std::shared_ptr<ChVisualShapeTriangleMesh> m_trimesh_shape;  ///< mesh visualization asset
    std::unique_ptr<ChColormap> m_colormap;                      ///< colormap for mesh false coloring
    ChColormap::Type m_colormap_type;                            ///< colormap type

    bool m_cosim_mode;  ///< co-simulation mode

    // SCM parameters
    double m_Bekker_Kphi;    ///< frictional modulus in Bekker model
    double m_Bekker_Kc;      ///< cohesive modulus in Bekker model
    double m_Bekker_n;       ///< exponent of sinkage in Bekker model (usually 0.6...1.8)
    double m_Mohr_cohesion;  ///< cohesion for shear failure [Pa]
    double m_Mohr_mu;        ///< coefficient of friction for shear failure [degree]
    double m_Janosi_shear;   ///< shear parameter in Janosi-Hanamoto formula [m]
    double m_elastic_K;      ///< elastic stiffness K per unit area, [Pa/m] (must be larger than Kphi)
    double m_damping_R;      ///< vertical damping R per unit area [Pa.s/m] (proportional to vertical speed)

    // Callback object for position-dependent soil properties
    std::shared_ptr<SCMTerrainOld::SoilParametersCallback> m_soil_fun;

    // Contact forces on contactable objects interacting with the SCM terrain
    std::unordered_map<ChBody*, std::pair<ChVector3d, ChVector3d>> m_body_forces;
    std::unordered_map<std::shared_ptr<fea::ChNodeFEAxyz>, ChVector3d> m_node_forces;

    // Bulldozing effects
    bool m_bulldozing;
    double m_flow_factor;
    double m_erosion_slope;
    int m_erosion_iterations;
    int m_erosion_propagations;

    // Mesh coloring mode
    SCMTerrainOld::DataPlotType m_plot_type;
    double m_plot_v_min;
    double m_plot_v_max;

    // Indices of visualization mesh vertices modified externally
    std::vector<int> m_external_modified_vertices;

    // Timers and counters
    ChTimer m_timer_active_domains;
    ChTimer m_timer_ray_testing;
    ChTimer m_timer_ray_casting;
    ChTimer m_timer_contact_patches;
    ChTimer m_timer_contact_forces;
    ChTimer m_timer_bulldozing;
    ChTimer m_timer_bulldozing_boundary;
    ChTimer m_timer_bulldozing_domain;
    ChTimer m_timer_bulldozing_erosion;
    ChTimer m_timer_visualization;
    int m_num_ray_casts;
    int m_num_ray_hits;
    int m_num_contact_patches;
    int m_num_erosion_nodes;

    friend class SCMTerrainOld;
    friend class ChScmVisualizationVSG;
};

/// @} vehicle_terrain

}  // end namespace vehicle
}  // end namespace chrono

#endif
