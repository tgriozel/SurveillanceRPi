from SimpleCV import Display, Image, VirtualCamera
import os, sys

if len(sys.argv) == 1:
	print("Give (at least one) file name(s) of the images(s) to process")
	sys.exit()

disp = Display((1280,1024))
virtual_cam = VirtualCamera(sys.argv[1], "image")
img = virtual_cam.getImage()
img.save(disp)

points = []
last_pt = (0, 0)

# Show the first image, the user had to left click the center of the donut,
# followed by the right inner and outer edge (in that order). Press "Esc" to exit.
# All the images are processed with the same parameters
while not disp.isDone():
    temp_pt = disp.leftButtonDownPosition()
    if( temp_pt != last_pt and temp_pt is not None):
        last_pt = temp_pt
        points.append(temp_pt)

# Center of the donut 
Cx = points[0][0]
Cy = points[0][1]
# Inner donut radius
R1x = points[1][0]
R1y = points[1][1]
R1 = R1x-Cx
# Outer donut radius
R2x = points[2][0]
R2y = points[2][1]
R2 = R2x-Cx

# Images transformation using ImageMagick
for i in range(1, len(sys.argv)):
	arg = sys.argv[i]
	os.system("convert %s -filter Cubic +distort DePolar '%d %d %d %d' out_%s" % (arg, R2, R1, Cx, Cy, arg))
