#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace apollo {

namespace common {
class TrajectoryPoint {
};
}  // namespace common

namespace hdmap {
class PathOverlap {
   public:
    double start_s;
    double end_s;
    std::string object_id;
};

class Path {
   public:
    std::vector<PathOverlap> pnc_junction_overlaps() { return std::vector<PathOverlap>(); }
};
}  // namespace hdmap

namespace planning {

enum StopReasonCode {
    STOP_REASON_STOP_SIGN = 101,
};

class TrafficLightStatus {
   public:
    int current_traffic_light_overlap_id_size() const { return 0; }
    std::string current_traffic_light_overlap_id(int) const { return ""; }
};

class StopSignStatus {
   public:
    std::string current_stop_sign_overlap_id() const { return ""; }
};

class PlanningStatus {
   public:
    TrafficLightStatus traffic_light() { return TrafficLightStatus(); }
    StopSignStatus stop_sign() { return StopSignStatus(); }
};

class PlanningContext {
   public:
    static PlanningContext* Instance() { return new PlanningContext(); }
    PlanningStatus planning_status() { return PlanningStatus(); }
};

class Task {
};

class TaskConfig {
   public:
    enum TaskType {
    };
};

class ScenarioBareIntersectionUnprotectedConfig {
   public:
    bool enable_explicit_stop() { return true; }
    double stop_distance() { return 0; }
    double approach_speed_limit() { return 0; }
};

class SLBoundery {
   public:
    double start_s() const { return 0; }
    double end_s() const { return 0; }
    double start_l() const { return 0; }
    double end_l() const { return 0; }
};

class STPoint {
   public:
    double s() { return 0; }
    double t() { return 0; }
};

class STBoundery {
   public:
    bool IsEmpty() const { return true; }
    double min_t() const { return 0; }
    double min_s() const { return 0; }
    STPoint bottom_left_point() const { return STPoint(); }
    STPoint bottom_right_point() const { return STPoint(); }
};

class Obstacle {
   public:
    std::string Id() const { return ""; }
    bool IsVirtual() { return false; }
    bool IsStatic() { return false; }
    const STBoundery reference_line_st_boundary() const { return STBoundery(); }
};

class PathDecision {
   public:
    std::vector<Obstacle*> obstacles() { return std::vector<Obstacle*>(); }
};

class ReferenceLine {
   public:
    hdmap::Path map_path() const { return hdmap::Path(); }
    void AddSpeedLimit(double start_s, double end_s, double speed_limit);
};

class ReferenceLineInfo {
   public:
    enum OverlapType {
        PNC_JUNCTION = 4,
        SIGNAL = 5,
        STOP_SIGN = 6,
    };
    ReferenceLine reference_line() const { return ReferenceLine(); }
    PathDecision path_decision() const { return PathDecision(); }
    SLBoundery AdcSlBoundary() const { return SLBoundery(); }
    ReferenceLine* mutable_reference_line() { return new ReferenceLine(); }
    void SetJunctionRightOfWay(const double junction_s, const bool is_protected) const { };
    int GetPnCJunction(const double s, hdmap::PathOverlap* pnc_junction_overlap) const { return 0; }
};

class Frame {
   public:
    std::list<ReferenceLineInfo> reference_line_info() const { return std::list<ReferenceLineInfo>(); }
    std::list<ReferenceLineInfo>* mutable_reference_line_info() { return new std::list<ReferenceLineInfo>(); }
};

namespace util {
    bool CheckInsidePnCJunction(const ReferenceLineInfo& reference_line_info) { return true; }
}

namespace scenario {

namespace util {

hdmap::PathOverlap* GetOverlapOnReferenceLine(
    const ReferenceLineInfo& reference_line_info, const std::string& overlap_id,
    const ReferenceLineInfo::OverlapType& overlap_type) {
    return new hdmap::PathOverlap();
}

}  // namespace util

class ScenarioConfig {
   public:
    enum ScenarioType {
        BARE_INTERSECTION_UNPROTECTED = 3,
        STOP_SIGN_PROTECTED = 4,
        STOP_SIGN_UNPROTECTED = 5,
        TRAFFIC_LIGHT_PROTECTED = 6,
        TRAFFIC_LIGHT_UNPROTECTED_LEFT_TURN = 7,
        TRAFFIC_LIGHT_UNPROTECTED_RIGHT_TURN = 8
    };
    enum StageType {
        BARE_INTERSECTION_UNPROTECTED_INTERSECTION_CRUISE = 202,
    };
    class StageConfig {
       public:
        StageType stage_type() const { return StageType(); }
    };
};

class Stage {
   public:
    enum StageStatus {
        ERROR = 1,
        READY = 2,
        RUNNING = 3,
        FINISHED = 4,
    };

    Stage(const ScenarioConfig::StageConfig& config);

    virtual ~Stage() = default;

    const ScenarioConfig::StageConfig& config() const { return config_; }

    ScenarioConfig::StageType stage_type() const { return config_.stage_type(); }

    /**
   * @brief Each stage does its business logic inside Process function.
   * If the stage want to transit to a different stage after finish,
   * it should set the type of 'next_stage_'.
   */
    virtual StageStatus Process(
        const common::TrajectoryPoint& planning_init_point, Frame* frame) = 0;

    /**
   * @brief The sequence of tasks inside the stage. These tasks usually will be
   * executed in order.
   */
    const std::vector<Task*>& TaskList() const { return task_list_; }

    const std::string& Name() const;

    template <typename T>
    T* GetContextAs() {
        return static_cast<T*>(context_);
    }

    void SetContext(void* context) { context_ = context; }

    Task* FindTask(TaskConfig::TaskType task_type) const;

    ScenarioConfig::StageType NextStage() const { return next_stage_; }

   protected:
    bool ExecuteTaskOnReferenceLine(
        const common::TrajectoryPoint& planning_start_point, Frame* frame);

    bool ExecuteTaskOnOpenSpace(Frame* frame);

    virtual Stage::StageStatus FinishScenario();

   protected:
    std::map<TaskConfig::TaskType, Task*> tasks_;
    std::vector<Task*> task_list_;
    ScenarioConfig::StageConfig config_;
    ScenarioConfig::StageType next_stage_;
    void* context_ = nullptr;
    std::string name_;
};

#define DECLARE_STAGE(NAME, CONTEXT)                              \
    class NAME : public Stage {                                   \
       public:                                                    \
        explicit NAME(const ScenarioConfig::StageConfig& config)  \
            : Stage(config) {}                                    \
        Stage::StageStatus Process(                               \
            const common::TrajectoryPoint& planning_init_point,   \
            Frame* frame) override;                               \
        CONTEXT* GetContext() { return GetContextAs<CONTEXT>(); } \
    }

class StageIntersectionCruiseImpl {
 public:
  bool CheckDone(const Frame& frame,
                 const ScenarioConfig::ScenarioType& scenario_type,
                 const ScenarioConfig::StageConfig& config,
                 const bool right_of_way_status);
};

bool StageIntersectionCruiseImpl::CheckDone(
    const Frame& frame,
    const ScenarioConfig::ScenarioType& scenario_type,
    const ScenarioConfig::StageConfig& config,
    const bool right_of_way_status) {
  const auto& reference_line_info = frame.reference_line_info().front();

  const auto& pnc_junction_overlaps =
      reference_line_info.reference_line().map_path().pnc_junction_overlaps();
  if (pnc_junction_overlaps.empty()) {
    // TODO(all): remove when pnc_junction completely available on map
    // pnc_junction not exist on map, use current traffic_sign's end_s
    // get traffic sign overlap along reference line
    hdmap::PathOverlap* traffic_sign_overlap = nullptr;
    if (scenario_type == ScenarioConfig::STOP_SIGN_PROTECTED ||
        scenario_type == ScenarioConfig::STOP_SIGN_UNPROTECTED) {
      // stop_sign scenarios
      const auto& stop_sign_status =
          PlanningContext::Instance()->planning_status().stop_sign();
      const std::string traffic_sign_overlap_id =
          stop_sign_status.current_stop_sign_overlap_id();
      traffic_sign_overlap = scenario::util::GetOverlapOnReferenceLine(
          reference_line_info,
          traffic_sign_overlap_id,
          ReferenceLineInfo::STOP_SIGN);
    } else if (scenario_type == ScenarioConfig::TRAFFIC_LIGHT_PROTECTED ||
               scenario_type ==
                   ScenarioConfig::TRAFFIC_LIGHT_UNPROTECTED_LEFT_TURN ||
               scenario_type ==
                   ScenarioConfig::TRAFFIC_LIGHT_UNPROTECTED_RIGHT_TURN) {
      // traffic_light scenarios
      const auto& traffic_light_status =
          PlanningContext::Instance()->planning_status().traffic_light();
      const std::string traffic_sign_overlap_id =
          traffic_light_status.current_traffic_light_overlap_id_size() > 0
              ? traffic_light_status.current_traffic_light_overlap_id(0)
              : "";
      traffic_sign_overlap = scenario::util::GetOverlapOnReferenceLine(
          reference_line_info,
          traffic_sign_overlap_id,
          ReferenceLineInfo::SIGNAL);
    } else {
      // TODO(all): to be added
      // yield_sign scenarios
    }

    if (!traffic_sign_overlap) {
      return true;
    }

    constexpr double kIntersectionPassDist = 20.0;  // unit: m
    const double adc_back_edge_s =
        reference_line_info.AdcSlBoundary().start_s();
    const double distance_adc_pass_traffic_sign =
        adc_back_edge_s - traffic_sign_overlap->end_s;

    // set right_of_way_status
    reference_line_info.SetJunctionRightOfWay(traffic_sign_overlap->start_s,
                                              right_of_way_status);

    return distance_adc_pass_traffic_sign >= kIntersectionPassDist;
  }

  if (!planning::util::CheckInsidePnCJunction(reference_line_info)) {
    return true;
  }

  // set right_of_way_status
  hdmap::PathOverlap pnc_junction_overlap;
  const double adc_front_edge_s = reference_line_info.AdcSlBoundary().end_s();
  reference_line_info.GetPnCJunction(adc_front_edge_s, &pnc_junction_overlap);
  reference_line_info.SetJunctionRightOfWay(pnc_junction_overlap.start_s,
                                            right_of_way_status);

  return false;
}

namespace bare_intersection {

struct BareIntersectionUnprotectedContext {
    ScenarioBareIntersectionUnprotectedConfig scenario_config;
    std::string current_pnc_junction_overlap_id;
};

class BareIntersectionUnprotectedStageIntersectionCruise : public Stage {
 public:
  explicit BareIntersectionUnprotectedStageIntersectionCruise(
      const ScenarioConfig::StageConfig& config)
      : Stage(config) {}

  Stage::StageStatus Process(const common::TrajectoryPoint& planning_init_point,
                             Frame* frame) override;

  BareIntersectionUnprotectedContext* GetContext() {
    return GetContextAs<BareIntersectionUnprotectedContext>();
  }

  Stage::StageStatus FinishStage();

 private:
  ScenarioBareIntersectionUnprotectedConfig scenario_config_;
  StageIntersectionCruiseImpl stage_impl_;
};

Stage::StageStatus BareIntersectionUnprotectedStageIntersectionCruise::Process(
    const common::TrajectoryPoint& planning_init_point, Frame* frame) {

  bool plan_ok = ExecuteTaskOnReferenceLine(planning_init_point, frame);

  bool stage_done = stage_impl_.CheckDone(
      *frame, ScenarioConfig::BARE_INTERSECTION_UNPROTECTED, config_, false);
  if (stage_done) {
    return FinishStage();
  }
  return Stage::RUNNING;
}

Stage::StageStatus
BareIntersectionUnprotectedStageIntersectionCruise::FinishStage() {
  return FinishScenario();
}

}  // namespace bare_intersection
}  // namespace scenario
}  // namespace planning
}  // namespace apollo

int main() {
    const apollo::planning::scenario::ScenarioConfig::StageConfig config = apollo::planning::scenario::ScenarioConfig::StageConfig();
    apollo::planning::scenario::bare_intersection::BareIntersectionUnprotectedStageIntersectionCruise s = apollo::planning::scenario::bare_intersection::BareIntersectionUnprotectedStageIntersectionCruise(config);
    apollo::common::TrajectoryPoint p = apollo::common::TrajectoryPoint();
    apollo::planning::Frame f = apollo::planning::Frame();
    s.Process(p, &f);
}