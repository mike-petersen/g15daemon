#define	TEXT_LEFT	1
#define NUM_BATS	3
#define	MAX_SCREENS	4
#define MAX_MODE        3
#define MAX_SUB_MODE    1

#define PAUSE           6
#define PAUSE_2         2*PAUSE
#define PAUSE_3         3*PAUSE
#define PAUSE_4         4*PAUSE
#define PAUSE_5         5*PAUSE

#define	VL_LEFT		42
#define	BAR_START	45
#define MAX_NET_HIST	107
#define	BAR_END		153
#define	TEXT_RIGHT	155
#define	MAX_LINES	128

#define CYCLE_CPU_SCREEN       0
#define CYCLE_MEM_SCREEN       1
#define CYCLE_SWAP_SCREEN      2
#define CYCLE_NET_SCREEN       3
#define CYCLE_BAT_SCREEN       4

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

