from .model import *
from .objects import *
from z3 import *


def api_in(a, b):
    if isinstance(a, Point) and isinstance(b, Line): 
        return is_point_on_line(a, b)
    elif isinstance(a, Point) and isinstance(b, Area): 
        return is_point_in_area(a, b)
    elif isinstance(a, Line) and isinstance(b, Area): 
        return is_line_in_area(a, b)
    elif isinstance(a, Area) and isinstance(b, Area): 
        return is_area_in_area(a, b)
    else:
        raise Exception("wrong constraint operator {}".format("in"))

def api_cross(a, b):
    if isinstance(a, Line) and isinstance(b, Line):
        return is_two_lines_cross(a, b)
    elif isinstance(a, Area) and isinstance(b, Area):
        return is_two_areas_cross(a, b)
    elif isinstance(a, Line) and isinstance(b, Area):
        return is_line_cross_area(a, b)
    elif isinstance(a, Area) and isinstance(b, Line):
        return is_line_cross_area(b, a)
    else:
        raise Exception("wrong constraint operator {}".format("cross"))

def api_is_color(traffic_light: TrafficLight, color: str):
    if not (isinstance(traffic_light, TrafficLight) and isinstance(color, str) and color in ["red", "yellow", "green", "unknown"]):
        raise Exception("wrong constraint operator {}".format("is_color"))
    if color == "green":
        return traffic_light.color > 1
    elif color == "yellow":
        return traffic_light.color == 1
    elif color == "red":
        return And(traffic_light.color < 1, traffic_light.color > 0)
    elif color == "unknown":
        return traffic_light.color <= 0
    else:
        raise Exception("wrong constraint operator {}".format("is_color"))

def api_is_direction(lane: Lane, direction: str):
    if not (isinstance(lane, Lane) and isinstance(direction, str) and direction in ["left", "straight", "right"]):
        raise Exception("wrong constraint operator {}".format("is_direction"))
    if direction == "left":
        return lane.turn < 0
    elif direction == "straight":
        return lane.turn == 0
    elif direction == "right":
        return lane.turn > 0
    else:
        raise Exception("wrong constraint operator {}".format("is_direction"))

def _api_st_location(ego: Vehicle, obs: MobileObject, flag=0):
    if flag == 0:
        return (- ego.size.l - obs.size.l - obs.pos.l) / obs.v.l * obs.v.s + obs.pos.s
    elif flag == 1:
        return (ego.size.l + obs.size.l - obs.pos.l) / obs.v.l * obs.v.s + obs.pos.s
    else:
        return None

def api_st_cross(ego: Vehicle, obs: MobileObject):
    a = _api_st_location(ego, obs, 0)
    b = _api_st_location(ego, obs, 1)
    return And(
        Or(
            And(a <= ego.pos.s + ego.size.s + obs.size.s, a >= ego.pos.s - ego.size.s - obs.size.s),
            And(b <= ego.pos.s + ego.size.s + obs.size.s, b >= ego.pos.s - ego.size.s - obs.size.s)
        ),
        api_cross(ego.trajectory, obs.trajectory)
    )

def api_st_above(ego: Vehicle, obs: MobileObject):
    a = _api_st_location(ego, obs, 0)
    b = _api_st_location(ego, obs, 1)
    return And(
        And(
            a <= ego.pos.s - ego.size.s - obs.size.s,
            b <= ego.pos.s - ego.size.s - obs.size.s
        ),
        api_cross(ego.trajectory, obs.trajectory)
    )

def api_st_below(ego: Vehicle, obs: MobileObject):
    a = _api_st_location(ego, obs, 0)
    b = _api_st_location(ego, obs, 1)
    return And(
        And(
            a >= ego.pos.s + ego.size.s + obs.size.s,
            b >= ego.pos.s + ego.size.s + obs.size.s
        ),
        api_cross(ego.trajectory, obs.trajectory)
    )