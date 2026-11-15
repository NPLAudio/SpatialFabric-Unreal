"""Engine-free four-room showcase plan. All distances are Unreal centimeters.
Rooms share a straight west-side hall. No fake controls, speaker models or billboard.
"""
ROOMS = (
    {'name':'Spatial motion','center_y':0,'bounds':(-1200,1200,-950,950),'height':900,'door_width':300},
    {'name':'Mono hit','center_y':1350,'bounds':(-1200,-200,950,1750),'height':420,'door_width':200},
    {'name':'Stereo groove','center_y':2150,'bounds':(-1200,-200,1750,2550),'height':420,'door_width':200},
    {'name':'Replay cue','center_y':2950,'bounds':(-1200,-200,2550,3350),'height':420,'door_width':200},
)
HALL = (-1750,-1200,-950,3350)
PLAYER_START = (-1480,-600,105)
PLAYER_YAW = 55
CUE_POSITIONS = ((-650,1350,110),(-650,2150,110),(-650,2950,110))
EDITOR_CAMERA = ((-1580,-650,190),(-3,55,0))


def geometry():
    pieces=[]
    def box(name,location,size,material='wall',collision=True):
        pieces.append({'name':name,'location':location,'size':size,'material':material,'collision':collision})
    box('Gallery foundation',(-275,1200,-20),(3050,4340,40),'floor')
    box('Hall west wall',(-1760,1200,180),(20,4340,360))
    for y in (-960,3360):box('Hall end '+str(y),(-1480,y,180),(560,20,360))
    box('Hall ceiling',(-1480,1200,365),(560,4340,10),'ceiling')
    box('Spatial back wall',(1210,0,450),(20,1920,900))
    for y in (-960,960):box('Spatial side '+str(y),(0,y,450),(2400,20,900))
    box('Spatial ceiling',(0,0,905),(2400,1920,10),'ceiling')
    box('Playback back wall',(-190,2150,210),(20,2400,420))
    for y in (1760,2560,3360):box('Playback partition '+str(y),(-700,y,210),(1000,20,420))
    box('Playback ceiling',(-700,2150,425),(1000,2400,10),'ceiling')
    for i,room in enumerate(ROOMS):
        _,_,lo,hi=room['bounds'];center=room['center_y'];half=room['door_width']/2;h=room['height']
        for side,a,b in [('left',lo,center-half),('right',center+half,hi)]:
            box(room['name']+' door '+side,(-1200,(a+b)/2,h/2),(20,b-a,h))
        box(room['name']+' lintel',(-1200,center,(280+h)/2),(20,room['door_width'],h-280))
        box(room['name']+' threshold',(-1190,center,1),(80,room['door_width']-30,2),'accent'+str(i),False)
    # Just the measured stage outline, not a decorative lattice or trip hazard.
    for y in (-750,750):box('Stage outline Y '+str(y),(0,y,1),(2000,4,2),'grid',False)
    for x in (-1000,1000):box('Stage outline X '+str(x),(x,0,1),(4,1500,2),'grid',False)
    return pieces


def lights():
    values=[('Spatial fill A',(-450,-300,780),18000,500,500,2200),
            ('Spatial fill B',(450,300,780),18000,500,500,2200)]
    for i,room in enumerate(ROOMS[1:]):values.append(('Playback fill '+str(i),(-700,room['center_y'],370),4200,200,200,1000))
    for i,y in enumerate((-500,1000,2500)):values.append(('Hall fill '+str(i),(-1480,y,315),4200,180,300,1200))
    return values


def signs():
    values=[('Hall guide','SPATIALFABRIC\nWASD + mouse\nAudio: Playback tab / OSC Controller',(-1747,-450,230),24,(0,0,0))]
    for i,room in enumerate(ROOMS):values.append(('Room '+str(i+1)+' name',str(i+1)+'  /  '+room['name'].upper(),(-1213,room['center_y'],325),30,(0,180,0)))
    values.extend([
        ('Spatial guide','4 tracked OSC objects / 3 animated\nWatch Object Tracker / Radar',(1197,0,260),28,(0,180,0)),
        ('Mono guide','Warm percussion / output 1\nPlay / Stop in Playback or OSC Controller\n/demo/playback/mono',(-213,1350,235),23,(0,180,0)),
        ('Stereo guide','Stereo groove / matching L-R\nLoop ON / OFF in Playback\n/demo/playback/stereo',(-213,2150,235),23,(0,180,0)),
        ('Replay guide','Glass cue / output 2\nAuto replay after 2.5 s; Stop cancels\n/demo/playback/replay',(-213,2950,235),23,(0,180,0)),
        ('Stage front axis','+X FRONT',(900,0,3),25,(90,0,0)),
        ('Stage right axis','+Y RIGHT',(0,650,3),25,(90,0,0)),
    ])
    return values


def floor_samples():
    return [('hall '+str(y),(-1480,y)) for y in (-700,0,1350,2150,2950,3200)]+[(room['name'],((-1200+room['bounds'][1])/2,room['center_y'])) for room in ROOMS]


def doorway_segments():
    return [(room['name'],(-1450,room['center_y'],100),(-1000,room['center_y'],100)) for room in ROOMS]


def wall_segments():
    return [('hall west',(-1500,0,100),(-1900,0,100)),
            ('hall south',(-1480,-800,100),(-1480,-1100,100)),
            ('hall north',(-1480,3200,100),(-1480,3550,100)),
            ('mono/stereo divider',(-650,1500,100),(-650,1950,100)),
            ('stereo/replay divider',(-650,2300,100),(-650,2750,100)),
            ('spatial/mono divider',(-650,800,100),(-650,1150,100)),
            ('spatial back',(1000,0,100),(1400,0,100))]
