use GD;
use G15Daemon;



printf "perl bindings version is %f\n",G15Daemon->VERSION;
printf "interface version is %f\n ",G15Daemon->apiversion;

my $width = G15Daemon->width();
my $height = G15Daemon->height();

# possible screentypes are 0 (1bpp pixel buffer) 1 (text - not implemented yet) or 2 (WBMP pixelbuffer)
my $a = G15Daemon->newscreen(2);

# create a new image
$im = new GD::Image ($width, $height);

# allocate some colors - these are the only valid ones for the G15
$white = $im->colorAllocate (0, 0, 0);
$black = $im->colorAllocate (255, 255, 255);

# fill with black
$im->fill(1,1,$black);

# Draw an oval
$im->arc (80, 22, 95, 42, 0, 360, $white);
# fill it with white
$im->fill(90,32,$white);

# draw a rectangle
$im->rectangle (10, 10, 150, 33, $white);
# fill it
$im->fill(12,12,$white);
$im->fill(145,12,$white);

$im->string(gdSmallFont,30,15,"G15Daemon & GDlib",$black);

my $wbmpdata = $im->wbmp ($black);
G15Daemon->send($a,$wbmpdata , length($wbmpdata));

sleep(60);
G15Daemon->closescreen($a);


