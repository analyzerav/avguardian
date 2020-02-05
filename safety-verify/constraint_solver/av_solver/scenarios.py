from z3 import *
from .model import *
from .objects import *

class Scenario:
    def __init__(self, configs=None):
        self.objects = {}
        self.constraints = []
        if configs is None:
            self.configs = {}
        else:
            self.configs = configs

        # init the ego vehicle (i.e., AV)
        ego = Vehicle("ego")
        ego.add_constraint(And(ego.pos.l == 0, ego.pos.s == 0))
        ego.add_constraint(And(ego.v.l == 0, ego.v.s > 0))
        self.add_object(ego)
        self.add_object(Destination(""))

        # Road and lane
        lane_left_width = Real("lane_left_width")
        lane_right_width = Real("lane_right_width")
        def lane_boundary(l, s):
            return And(s > 0, l >= -lane_left_width, l <= lane_right_width)
        self.lane_boundary = Area(lane_boundary)
        self.add_constraint(And(lane_left_width > 0, lane_right_width > 0, lane_left_width + lane_right_width >= 3.3))

        road_left_width = Real("road_left_width")
        road_right_width = Real("road_right_width")
        def road_boundary(l, s):
            return And(s > 0, l >= -road_left_width, l <= road_right_width)
        self.road_boundary = Area(road_boundary)
        self.add_constraint(road_left_width > lane_left_width)
        self.add_constraint(road_right_width > lane_right_width)
        self.add_constraint(And(road_left_width > 0, road_right_width > 0, road_left_width + road_right_width >= 9.9))

        #config
        self.max_stop_deceleration = 10

    def add_object(self, obj, name=None):
        if name is None:
            self.objects[obj.name] = obj
        else:
            self.objects[name] = obj

    def add_constraint(self, c):
        self.constraints.append(c)

    @property
    def ego_vehicle(self):
        return self.objects["vehicle_ego"]

    def is_ego(self, obj):
        return isinstance(obj, Vehicle) and obj.name == "vehicle_ego"

    def get_object(self, classtype):
        for name in self.objects:
            if isinstance(self.objects[name], classtype) and not self.is_ego(self.objects[name]):
                return self.objects[name]
        raise Exception("Not found")

    def get_objects(self, classtype):
        results = []
        for name in self.objects:
            if isinstance(self.objects[name], classtype):
                results.append(self.objects[name])
        return results

    def get_variables(self):
        for a in dir(self):
            if not a.startswith("__") and not callable(getattr(self, a)):
                if a not in ["objects", "constraints", "configs"]:
                    yield a, getattr(self, a)
        for name in self.objects:
            obj = self.objects[name]
            yield name, obj
            for n, v in obj.get_variables():
                yield n, v

    def get_constraints(self):
        for c in self.constraints:
            yield c
        for name in self.objects:
            obj = self.objects[name]
            for c in obj.constraints:
                yield c


class CrosswalkScenario(Scenario):
    def __init__(self, configs=None):
        super().__init__(configs)

        self.add_object(Crosswalk("0"))
        self.add_object(Pedestrian("0"))
        self.add_object(Vehicle("0"))


class TrafficLightScenario(Scenario):
    def __init__(self, configs=None):
        super().__init__(configs)

        self.add_object(TrafficLight("self"))
        self.add_object(TrafficLight("main"))


class StopSignScenario(Scenario):
    def __init__(self, configs=None):
        super().__init__(configs)

        self.add_object(StopSign("0"))


class IntersectionScenario(Scenario):
    def __init__(self, configs=None):
        super().__init__(configs)

        if configs is None:
            self.configs = {
                "way_num": 4,
                "layout": [
                    [],[],[],[]
                ]
            }
        
        self.add_object(Intersection(""))
        self.add_object(Vehicle("0"))
        self.add_constraint(self.ego_vehicle.lane.way_id == 0)
        
        vl = self.get_objects(Vehicle)
        for v in vl:
            self.add_constraint(And(v.lane.way_id >= 0, v.lane.way_id < self.configs["way_num"]))
            # self.add_constraint(And(v.lane.turn >= 0, v.lane.turn < self.configs["way_num"]))

    
class StopSignIntersectionScenario(IntersectionScenario, StopSignScenario):
    def __init__(self, configs=None):
        super(StopSignIntersectionScenario, self).__init__(configs)


class TrafficLightIntersectionScenario(IntersectionScenario, TrafficLightScenario):
    def __init__(self, configs=None):
        super(TrafficLightIntersectionScenario, self).__init__(configs)


class FullScenario(IntersectionScenario, TrafficLightScenario, StopSignScenario, CrosswalkScenario):
    def __init__(self, configs=None):
        super(FullScenario, self).__init__(configs)