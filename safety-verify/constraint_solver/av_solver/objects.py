from .model import *
from z3 import *


class ModelObject:
    def __init__(self, name=""):
        self.name = name
        self.constraints = []

    def z3_name(self, var_name):
        return "{}.{}".format(self.name, var_name)

    def obj_name(self, prefix, name):
        if name == "":
            return prefix
        else:
            return "{}_{}".format(prefix, name)

    def add_constraint(self, c):
        self.constraints.append(c)

    def __repr__(self):
        return self.name

    def __str__(self):
        return self.name

    def get_variables(self, obj=None, prefix=None):
        if obj is None:
            obj = self
        if prefix is None:
            prefix = obj.name
        for a in dir(obj):
            attr = getattr(obj, a)
            if not a.startswith("__") and not callable(attr):
                if a not in ["name", "constraints"]:
                    yield "{}.{}".format(prefix, a), attr
            if isinstance(attr, BaseModel) or isinstance(attr, ModelObject):
                for a, attr in self.get_variables(obj=attr, prefix="{}.{}".format(prefix, a)):
                    yield a, attr


class Lane(ModelObject):
    def __init__(self, name=""):
        super().__init__(name)

        self.way_id = Real(self.z3_name("way_id"))
        self.road_id = Real(self.z3_name("road_id"))
        self.lane_id = Real(self.z3_name("lane_id"))
        # way id of destination way
        self.turn = Real(self.z3_name("turn"))


class StaticObject(ModelObject):
    def __init__(self, name=""):
        super().__init__(name)

        self.pos = Point(Real(self.z3_name("pos_l")),
                         Real(self.z3_name("pos_s")))
        self.size = Point(Real(self.z3_name("size_l")),
                          Real(self.z3_name("size_s")))
        self.boundary = CenterArea(self.pos, self.size)

        self.start_l = self.pos.l - self.size.l
        self.end_l = self.pos.l + self.size.l
        self.start_s = self.pos.s - self.size.s
        self.end_s = self.pos.s + self.size.s

        self.add_constraint(And(self.size.l > 0, self.size.s > 0))


class MobileObject(StaticObject):
    def __init__(self, name=""):
        super().__init__(name)

        self.v = Point(Real(self.z3_name("v_l")),
                       Real(self.z3_name("v_s")))
        self.velocity = self.v.l * self.v.l + self.v.s * self.v.s
        self.trajectory = get_trajectory(
            self.pos, self.v, 7, self.z3_name("trajectory"))


class Pedestrian(MobileObject):
    def __init__(self, name=""):
        super().__init__(self.obj_name("pedestrian", name))

        self.add_constraint(And(self.size.l == 0.5, self.size.s == 0.5))
        self.add_constraint(self.velocity <= 5)


class Vehicle(MobileObject):
    def __init__(self, name=""):
        super().__init__(self.obj_name("vehicle", name))

        self.add_constraint(And(self.size.l == 2, self.size.s == 2))
        self.add_constraint(self.velocity <= 80)

        self.lane = Lane(self.z3_name("lane"))
        self.arrive_time = Real(self.z3_name("arrive_time"))


class Destination(StaticObject):
    def __init__(self, name=""):
        super().__init__(self.obj_name("destination", name))
        self.add_constraint(And(self.size.l == 2, self.size.s == 2))


class Crosswalk(StaticObject):
    def __init__(self, name=""):
        super().__init__(self.obj_name("crosswalk", name))

        self.add_constraint(And(self.size.s == 2, self.size.l >= 5))
        self.add_constraint(And(self.pos.s >= 5, self.pos.s < 50))
        self.add_constraint(self.start_l * self.end_l <= 0)


class TrafficLight(StaticObject):
    def __init__(self, name=""):
        super().__init__(self.obj_name("traffic_light", name))

        # color: >1 green; =1 yellow; <1 red =0 unknown;
        self.color = Real(self.z3_name("color"))
        # self.lane = Lane(self.z3_name("lane"))


class StopSign(StaticObject):
    def __init__(self, name=""):
        super().__init__(self.obj_name("stop_sign", name))

        self.lane = Lane(self.z3_name("lane"))


class YieldSign(StaticObject):
    def __init__(self, name=""):
        super().__init__(self.obj_name("yield_sign", name))

        self.lane = Lane(self.z3_name("lane"))


class Intersection(StaticObject):
    def __init__(self, name=""):
        super().__init__(self.obj_name("intersection", name))

        self.add_constraint(And(self.size.s >= 10, self.size.l >= 5))
