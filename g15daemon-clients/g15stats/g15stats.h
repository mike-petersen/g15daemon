#define	TEXT_LEFT	1
#define NUM_BATS	3
#define NUM_TEMP	3
#define MAX_SENSOR      3
#define	MAX_SCREENS	5
#define MAX_MODE        3
#define MAX_SUB_MODE    1

#define PAUSE           6
#define PAUSE_2         2*PAUSE
#define PAUSE_3         3*PAUSE
#define PAUSE_4         4*PAUSE
#define PAUSE_5         5*PAUSE
#define PAUSE_6         6*PAUSE
#define PAUSE_7         7*PAUSE

#define	VL_LEFT		42
#define	BAR_START	45
#define MAX_NET_HIST	107
#define	BAR_END		153
#define	TEXT_RIGHT	155
#define	MAX_LINES	128

#define SCREEN_CPU_SCREEN       0
#define SCREEN_MEM_SCREEN       1
#define SCREEN_SWAP_SCREEN      2
#define SCREEN_NET_SCREEN       3
#define SCREEN_BAT_SCREEN       4
#define SCREEN_TEMP_SCREEN      5

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
