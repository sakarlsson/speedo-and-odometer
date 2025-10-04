# inkscape

def frange(start, stop, step):
    while start < stop:
        yield start
        start += step
	

def adjust_textpos(text, font_size):
    l = len(text)
    return -(l*font_size/4), font_size/2
    # return -(l/4*font_size/2),font_size/2

####################################################################
# Use Simple Inkscape Scripting to draw an unnumbered speedometer. #
####################################################################

# Define some drawing parameters.
r1 = 40
r2 = r1*0.93
r_small = r1*0.88
r_medium = r1*0.82
r_large = r1*0.80
r_txt = r1*0.62
cx, cy = canvas.width/2, canvas.height/2

# Draw the background.
circle((cx, cy), r1, fill='black')
ang1, ang2 = pi*0.75, pi*2.25

start_value=0
end_value=160
small_dash_increment=1
medium_dash_increment=5
large_dash_increment=20

valued_increment=20              # for x dash make it valued
sweep_start=90
sweep_end=90+45+270

sweep_deg = sweep_end - sweep_start
ticks = (end_value - start_value) / small_dash_increment
tick_deg = sweep_deg / ticks

font_size=10                  # px

value = start_value
for ang_deg in frange(sweep_start, sweep_end, tick_deg):
    ang = 2*pi*(ang_deg/360)
    value += small_dash_increment

    # Compute the outer and inner coordinates of each tick.
    rr = r_small
    thick = 0.3
    if value % medium_dash_increment == 0:
        thick = 0.5
        rr = r_medium
    if value % large_dash_increment == 0:
        thick = 1.0
        rr = r_large

    # tick_ang = (ang2 - ang1)*tick_ang/240 + ang1
    x1 = r2*cos(ang) + cx
    y1 = r2*sin(ang) + cy
    x2 = rr*cos(ang) + cx
    y2 = rr*sin(ang) + cy
    x_txt = r_txt*cos(ang) + cx
    y_txt = r_txt*sin(ang) + cy
    # print(ang, value, x1-cx, y1-cy)

    clr = '#eef4d7'
    # if ang >= pi*1.74:
    #     clr = 'red'

    if value % valued_increment == 0:
        (x_adj, y_adj) = adjust_textpos(str(value), font_size)
        text(f"{value}", (x_txt + x_adj, y_txt + y_adj), fill='#eef4d7', 
             font_size=f"{font_size}px", 
             font_family='Comfortaa', 
             font_weight='bold')

    line((x1, y1), (x2, y2),
         stroke_width=thick, stroke=clr, stroke_linecap='round')

# # Draw the surrounding edge.
# arc((cx, cy), r2, (ang1, ang2),
#     stroke_width=15, stroke='white', stroke_linecap='square')

# # Draw the needle.
# ang = pi*1.3
# x = r3*cos(ang) + cx
# y = r3*sin(ang) + cy
# line((cx, cy), (x, y), stroke_width=8, stroke='orange')
# circle((cx, cy), r5, fill='#303030')
