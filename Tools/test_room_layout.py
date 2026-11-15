"""Engine-free geometry/navigation checks; real pawn sweeps run in example smoke."""
import importlib.util
import math
from pathlib import Path
import unittest

PATH=Path(__file__).parent/'Unreal/room_layout.py'
SPEC=importlib.util.spec_from_file_location('sf_rooms_test',PATH)
PLAN=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(PLAN)


def intersects(start,end,piece,inflate=(0,0,0)):
    low,high=0.,1.
    for axis in range(3):
        center=piece['location'][axis];half=piece['size'][axis]/2+inflate[axis]
        lo,hi=center-half,center+half;direction=end[axis]-start[axis]
        if abs(direction)<1e-8:
            if not lo<=start[axis]<=hi:return False
        else:
            a,b=(lo-start[axis])/direction,(hi-start[axis])/direction
            low=max(low,min(a,b));high=min(high,max(a,b))
            if low>high:return False
    return True


class RoomLayoutTests(unittest.TestCase):
    def test_compact_owned_scene_and_unique_geometry(self):
        pieces=PLAN.geometry();self.assertEqual(len({p['name'] for p in pieces}),len(pieces))
        self.assertEqual(len(PLAN.ROOMS),4)
        self.assertEqual(13+len(pieces)+len(PLAN.lights())+len(PLAN.signs()),66)
        self.assertEqual(len(PLAN.signs()),11)
        self.assertTrue(all(min(p['size'])>0 for p in pieces))
        self.assertFalse(any('Speaker' in p['name'] or 'Billboard' in p['name'] for p in pieces))

    def test_capsule_has_clear_hall_and_each_door(self):
        solids=[p for p in PLAN.geometry() if p['collision']]
        paths=PLAN.doorway_segments()+[('hall',(-1480,-700,90),(-1480,3200,90))]
        for name,start,end in paths:
            start=(start[0],start[1],90);end=(end[0],end[1],90)
            hits=[p['name'] for p in solids if intersects(start,end,p,(34,34,88))]
            self.assertEqual(hits,[],name)

    def test_walls_isolate_rooms_and_floor_covers_route(self):
        pieces=PLAN.geometry();solids=[p for p in pieces if p['collision']]
        for name,start,end in PLAN.wall_segments():self.assertTrue(any(intersects(start,end,p) for p in solids),name)
        floor=next(p for p in pieces if p['name']=='Gallery foundation')
        for name,(x,y) in PLAN.floor_samples():self.assertTrue(intersects((x,y,200),(x,y,-100),floor),name)
        for room,position in zip(PLAN.ROOMS[1:],PLAN.CUE_POSITIONS):
            x,y,_=position;lo_x,hi_x,lo_y,hi_y=room['bounds'];self.assertTrue(lo_x<x<hi_x and lo_y<y<hi_y)

    def test_full_motion_cycle_stays_inside_spatial_room(self):
        lo_x,hi_x,lo_y,hi_y=PLAN.ROOMS[0]['bounds']
        for frame in range(901):
            angle=frame/900*2*math.pi
            for x,y,z in [(700*math.cos(angle),500*math.sin(angle),320+160*math.sin(2*angle)),(650*math.sin(angle),450*math.sin(2*angle),220),(0,550*math.cos(angle),400+220*math.sin(angle))]:
                self.assertTrue(lo_x+35<x<hi_x-35 and lo_y+35<y<hi_y-35 and 35<z<865)


if __name__=='__main__':unittest.main()
