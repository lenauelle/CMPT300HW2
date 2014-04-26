/////////////////////////////////////////////////////////////////

#define MIN_TOOLS 2
#define MAX_TOOLS 100
#define DEFAULT_TOOLS 3
#define MIN_OPERATORS 1
#define MAX_OPERATORS 100
#define DEFAULT_OPERATORS 3
#define NUM_GENERATORS 3
#define BUFFER_SIZE 10
#define BUFFER_REDUCE 3
#define MATERIAL_TYPE 3
#define PRODUCT_TYPE 3
#define STRING_SIZE 100

typedef enum { MatX, MatY, MatZ } MATERIAL;
typedef enum { ProA, ProB, ProC } PRODUCT;
typedef enum { false, true } bool;
typedef int item;

char** material_name[MATERIAL_TYPE] = {"X", "Y", "Z"};
char** product_name[PRODUCT_TYPE] = {"A (X+Y)", "B (Y+Z)", "C (Z+X)"};

/////////////////////////////////////////////////////////////////

/* Variables: Program */

pthread_mutex_t mutex_buffer, mutex_tool, mutex_output;
pthread_mutex_t mutex_generator[NUM_GENERATORS];
pthread_mutex_t mutex_operator[MAX_OPERATORS];
//full: 0->full; empty: full->0; 
//tool: number of sets->0; material_available: 0->1
sem_t full, empty, tool, material_available;
//condition variables are used in pause/resume
pthread_cond_t cond_generator[NUM_GENERATORS], cond_operator[MAX_OPERATORS];

item buffer[BUFFER_SIZE];
int buffer_quantity[MATERIAL_TYPE]; //quantity of materials
bool buffer_status[MATERIAL_TYPE]; //if there is such kind of material
int counter; //buffer counter

long output_quantity[PRODUCT_TYPE]; //quantity of products
PRODUCT latest_product;

int num_operators;
int num_tools;
int num_deadlocks;
int free_tools;
   
//quantity of materials (includes discarded)
long material_generated[MATERIAL_TYPE]; 
//quantity of products (includes discarded)
long product_produced[PRODUCT_TYPE]; 

bool bpaused; //flag: do Pause or Resume
bool btool[MAX_OPERATORS]; //flag: already has 2 tools or not
bool bactive; //flag: if the program active or not

pthread_t generatorid[NUM_GENERATORS], operatorid[MAX_OPERATORS], timerid;
pthread_attr_t attr;

/////////////////////////////////////////////////////////////////

/* Functions: Threads */

void *generators (void *param);
void *operators (void *param);
void *timer (void *param);

/////////////////////////////////////////////////////////////////

