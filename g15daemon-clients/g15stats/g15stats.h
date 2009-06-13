#define	TEXT_LEFT	1
#define NUM_BATS	3
#define NUM_TEMP	3
#define MAX_SENSOR      3
#define	MAX_SCREENS	5
#define MAX_MODE        3
#define MAX_SUB_MODE    1

#define PAUSE           6

#define	VL_LEFT		42
#define	BAR_START	45
#define MAX_NET_HIST	107
#define	BAR_END		153
#define BAR_BOTTOM      32
#define	TEXT_RIGHT	155
#define	MAX_LINES	128
#define BOTTOM_ROW      37

#define SENSOR_ERROR    -100

#define DIRECTION_DOWN   1
#define DIRECTION_UP     2

#define SCREEN_CPU       0
#define SCREEN_MEM       1
#define SCREEN_SWAP      2
#define SCREEN_NET       3
#define SCREEN_BAT       4
#define SCREEN_TEMP      5
#define SCREEN_FREQ      6
#define SCREEN_TIME      7

#define MODE_CPU_USR_SYS_NCE_1 0
#define MODE_CPU_TOTAL         1
#define MODE_CPU_USR_SYS_NCE_2 2
#define MODE_CPU_SUMARY        3

typedef struct g15_stats_bat_info
{	
	long	max_charge;
	long	cur_charge;
	long	status;
} g15_stats_bat_info;

typedef struct g15_stats_temp_info
{
	float	max_temp;
	float	cur_temp;
} g15_stats_temp_info;
