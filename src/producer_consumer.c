#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include "producer_consumer"

/////////////////////////////////////////////////////////////////

/* Access functions */

int sumStatus() {
   return buffer_status[MatX] + buffer_status[MatY] + buffer_status[MatZ];
}

int addMaterial(int x) {
   return (x + 1) % MATERIAL_TYPE;
}

void bufferReduce(MATERIAL m) {
   while (buffer_quantity[m] > BUFFER_REDUCE) {
      buffer_quantity[m] -= 1;
      counter -= 1;
      sem_post(&empty);
      sem_trywait(&full);
   }
}

void initInt(int m[], int term, int value) {
   int i;
   for(i = 0; i < term; i++) {
      m[i] = value;
   }
}

void initLong(long m[], int term, long value) {
   int i;
   for(i = 0; i < term; i++) {
      m[i] = value;
   }
}

/////////////////////////////////////////////////////////////////

/* Initialization */

void initializeStatic() {

   int i;

   initLong(output_quantity, PRODUCT_TYPE, 0);
   initLong(material_generated, MATERIAL_TYPE, 0);
   initLong(product_produced, PRODUCT_TYPE, 0);

   latest_product = -1; //-1: no products produced yet
   num_deadlocks = 0;
   bpaused = false;
   bactive = false;

   while(1) {
      printf("<?> Please set the number of operators: ");
      if(!scanf("%d", &num_operators)) {
          printf("<!> Invalid input. Use default setting: %d operators.\n", DEFAULT_OPERATORS);
          num_operators = DEFAULT_OPERATORS;
          break;
      }
      else if(num_operators > MAX_OPERATORS || num_operators < MIN_OPERATORS) {
         printf("<!> Please input number between %d and %d.\n", MIN_OPERATORS, MAX_OPERATORS);
      }
      else break;
   }

   while(1) {
      printf("<?> Please set the number of tools: ");
      if(!scanf("%d", &num_tools)) {
          printf("<!> Invalid input. Use default setting: %d tools.\n", DEFAULT_TOOLS);
          num_tools = DEFAULT_TOOLS;
          break;
      }
      else if(num_tools > MAX_TOOLS || num_tools < MIN_TOOLS) {
         printf("<!> Please input number between %d and %d.\n", MIN_TOOLS, MAX_TOOLS);
      }
      else break;
   }

   for (i = 0; i < NUM_GENERATORS; i++) {
      pthread_mutex_init(&mutex_generator[i], NULL);
      pthread_cond_init(&cond_generator[i], NULL);
   } 
   for (i = 0; i < num_operators; i++) {
      pthread_mutex_init(&mutex_operator[i], NULL);
      pthread_cond_init(&cond_operator[i], NULL);
   }

   pthread_attr_init(&attr);
}

void initializeDynamic() {

   counter = 0;
   free_tools = num_tools;

   initInt(buffer, BUFFER_SIZE, -1); //-1: no items inside yet
   initInt(buffer_quantity, MATERIAL_TYPE, 0);
   initInt(buffer_status, MATERIAL_TYPE, false);
   initInt(btool, MAX_OPERATORS, false);

   pthread_mutex_init(&mutex_buffer, NULL);
   pthread_mutex_init(&mutex_tool, NULL);
   pthread_mutex_init(&mutex_output, NULL);
   sem_init(&full, 0, 0);
   sem_init(&empty, 0, BUFFER_SIZE);
   //material_available value 0/1 -> <2/>=2 material(s) in buffer
   sem_init(&material_available, 0, 0); 
   sem_init(&tool, 0, num_operators/2); 
}

/////////////////////////////////////////////////////////////////

/* Action: Generate material */

bool generateMaterial(MATERIAL m) {
   int types, i, s_value;

   if (counter < BUFFER_SIZE) {
      //add to material generated record
      material_generated[m]++;
      printf("<*> Generated: Material %s\n", material_name[m]);
      //put material into buffer
      buffer_quantity[m]++;
      counter++;
      if (buffer_status[m] == false)
         buffer_status[m] = true;
      //update availability flag
      types = sumStatus();
      sem_getvalue(&material_available, &s_value);
      if (types > 1 && s_value == 0) {
         //if there are 2 types of materials in buffer now
         //and the semaphore is not updated yet
         sem_post(&material_available); //increase to 1
      }

      //check current buffer status to ensure all 3 types are in buffer
      if (counter == BUFFER_SIZE) {          
          if (types <= 2) {
            //discard in-buffer materials to not more than 3
            printf("<!> Buffer is full now, but not all types of materials are in buffer. Discard some. :(\n");
            bufferReduce(MatX);
            bufferReduce(MatY);
            bufferReduce(MatZ);
          }
      }
      return true;
   }
   else {
      return false;
   }
}

/////////////////////////////////////////////////////////////////

/* Action: Grab tool */

bool grabTool(int n) {
   if (free_tools >= 2) {
      free_tools -= 2;
      btool[n] = true;
      return true;
   }
   else {
      return false;
   }
}

/////////////////////////////////////////////////////////////////

/* Action: Release tool */

void releaseTool(int n) {
   btool[n] = false;
   free_tools += 2;
}

/////////////////////////////////////////////////////////////////

/* Action: Grab and consume materials */

PRODUCT operateMaterial() {
   MATERIAL m1, m2;
   PRODUCT p;
   int i = 0;

   if (counter > 0 && sumStatus() > 1) {
      m1 = addMaterial(latest_product);
      m2 = addMaterial(m1);
      while(!((buffer_status[m1] == true) && (buffer_status[m2] == true))) {
         m1 = addMaterial(m1);
         m2 = addMaterial(m2);
         i++;
         if (i > 2) {
            return -1;
         }
      }
      //consume the material to make product
      sleep(rand() / (float)RAND_MAX * 0.99 + 0.01); //time cost: operation
      buffer_quantity[m1] -= 1;
      buffer_quantity[m2] -= 1;
      p = m1;
      //record this produce
      product_produced[p]++;
      printf("<*> Produced: Product %s\n", product_name[p]);
      //update buffer status
      counter -= 2;
      if (buffer_quantity[m1] < 1)
         buffer_status[m1] = false;
      if (buffer_quantity[m2] < 1)
         buffer_status[m2] = false;
      //update availability flag
      if (sumStatus() > 1) {
         //if still >=2 types of materials are in buffer
         sem_post(&material_available); //cancel the decrement
      }
      return p;
   }
   else {
      return -1;
   }
}

/////////////////////////////////////////////////////////////////

/* Action: Output product */

bool outputProduct(PRODUCT p1) {
   int temp = output_quantity[p1] + 1;
   PRODUCT p2 = (p1 + 1) % PRODUCT_TYPE;
   PRODUCT p3 = (p1 + 2) % PRODUCT_TYPE;
   //Constraints: 
   //1) No same products can be next to each other 
   //2) The difference of the number of any two kinds of products produced should be less than 10
   if (p1 != latest_product && abs(temp - output_quantity[p2]) < 10 && abs(temp - output_quantity[p3]) < 10) {
      //add to the output queue
      printf("<*> Output: Product %s\n", product_name[p1]);
      output_quantity[p1]++;
      //update the latest product
      latest_product = p1;
      return true;
   }
   else {
      return false;
   }
}

/////////////////////////////////////////////////////////////////

/* Thread: Generator */

void *generators (void *param) {
   int n = (int)param; //generator id
   item m = (item)n; //material
   int oldState = 0;
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,&oldState);
   while(1) {
      if(!bpaused) { 
         //pthread_testcancel();
         sleep(rand() / (float)RAND_MAX + 1); //time cost: generation
         sem_wait(&empty);
         pthread_mutex_lock(&mutex_buffer);
         if (!generateMaterial(m)) {
            printf("<!> Generation problem :(\n");
         }
         pthread_mutex_unlock(&mutex_buffer);
         sem_post(&full);
         bactive = true;
      }
      else {
         pthread_mutex_lock(&mutex_generator[n]);
         pthread_cond_wait(&cond_generator[n], &mutex_generator[n]);
         pthread_mutex_unlock(&mutex_generator[n]);
      }
   }
};

/////////////////////////////////////////////////////////////////

/* Thread: Operator */

void *operators (void *param) {
   int n = (int)param; //operator id
   int oldState = 0;
   item p; //product
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,&oldState);
   while(1) {
      if(!bpaused) {
         //pthread_testcancel();
         sleep(rand() / (float)RAND_MAX + 1); //time cost: getting tool
         sem_wait(&tool);
         pthread_mutex_lock(&mutex_tool);
         if (!grabTool(n)) {
            printf("<!> Operator %d: Disable to get tools :(\n", n);
            pthread_mutex_unlock(&mutex_tool);
         }
         else { //success
            pthread_mutex_unlock(&mutex_tool);
            sem_wait(&material_available);
            sem_wait(&full);
            sem_wait(&full); //have to decrease by 2
            pthread_mutex_lock(&mutex_buffer);
            p = operateMaterial();
            if (p == -1) {
               printf("<!> Not enough materials :(\n");
            }
            else { //success
               pthread_mutex_lock(&mutex_tool);
               releaseTool(n);
               pthread_mutex_unlock(&mutex_tool);
            }
            pthread_mutex_unlock(&mutex_buffer);
            sem_post(&empty);
            sem_post(&empty); //also, increase by 2
         }
         sem_post(&tool);
         pthread_mutex_lock(&mutex_output);
         if (!outputProduct(p)) {
            printf("<!> Illegal to output %s; it will be discarded.\n", product_name[p]);
         }
         pthread_mutex_unlock(&mutex_output);
         bactive = true;
      }
      else {
         pthread_mutex_lock(&mutex_operator[n]);
         pthread_cond_wait(&cond_operator[n], &mutex_operator[n]);
         pthread_mutex_unlock(&mutex_operator[n]);
      }
   }
};

/////////////////////////////////////////////////////////////////

/* Thread: Timer */

void *timer (void *param) {
   int count = 0;
   int oldState = 0;
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,&oldState);
   while(1) {
      sleep(1);
      if (!bactive) {
         count++;
      }
      else {
         count = 0;
         bactive = false;
      }
      //timeout interval: 10s
      if (count >= 10 && bpaused) {
         count = 0; //do not regard it as timeout if paused
      }
      else if (count >= 10 && !bpaused) {
         printf("<!> Timeout: deadlock occurs\n");
         //record deadlock
         num_deadlocks++;
         //restart all threads without changing the current restored status information
         cancelThreads();
         initializeDynamic();
         createThreads();
      }
   }
}

/////////////////////////////////////////////////////////////////

/* Create threads */

void createThreads () {
   int i;
   //create generators
   for (i = 0; i < NUM_GENERATORS; i++) {
      pthread_create(&generatorid[i], &attr, generators, (void*) i);
   }
   //create operators
   for (i = 0; i < num_operators; i++) {
      pthread_create(&operatorid[i], &attr, operators, (void*) i); 
   }
}

/////////////////////////////////////////////////////////////////

/* Cancel threads */

void cancelThreads() {
   int i;
   //cancel generators
   for (i = 0; i < 3; i++) {
      pthread_cancel(generatorid[i]);
   }
   //cancel operators
   for (i = 0; i < num_operators; i++) {
      pthread_cancel(operatorid[i]);
   }
}

/////////////////////////////////////////////////////////////////

/* Manual */

void showManual() {
   printf("/////////////////////////////////////////////////////////////////\n");
   printf("Manual:\nInput 'u' to pause/resume;\n");
   printf("Input 'm' to see how many materials are generated;\n");
   printf("Input 'b' to see the current status of the buffer;\n");
   printf("Input 'p' to see how many products are produces;\n");
   printf("Input 'q' to see the current status of the output queue;\n");
   printf("Input 'd' to see how many times that deadlocks occurred;\n");
   printf("Input 'h' to see this manual again;\n");
   printf("Input 'restart' to clear all records and restart;\n");
   printf("Input 'exit' to exit the process.\n");
   printf("/////////////////////////////////////////////////////////////////\n");
   sleep(2);
}

/////////////////////////////////////////////////////////////////

int main() {
   char command[STRING_SIZE];
   long sum;
   int i;

   printf("\n");
   initializeStatic();
   initializeDynamic();
   showManual();
   pthread_create(&timerid, &attr, timer, NULL);
   createThreads();
   while(1) {
      if(scanf("%s", &command)) {
         if(!strcmp(command, "u")) { 
            if (!bpaused) {
              //do: pause
              bpaused = true;
              printf("<*> -----------Pause-----------\n");
            }
            else {
               //do: resume
               bpaused = false;
               printf("<*>-----------Resume-----------\n");
               for (i = 0; i < NUM_GENERATORS; i++) {
                  pthread_cond_signal(&cond_generator[i]);
               }
               for (i = 0; i < num_operators; i++) {
                  pthread_cond_signal(&cond_operator[i]);
               }
            }
         }
         else if(!strcmp(command, "m")) {
            sum = material_generated[MatX] + material_generated[MatY] + material_generated[MatZ];
            printf("/////////////////////////////////////////////////////////////////\n");
            printf("Total generated: %ld\n", sum);
            printf("Material X: %ld; Material Y: %ld; Material Z: %ld.\n", material_generated[MatX], material_generated[MatY], material_generated[MatZ]);
            printf("/////////////////////////////////////////////////////////////////\n");
         }
         else if(!strcmp(command, "b")) {
            printf("/////////////////////////////////////////////////////////////////\n");
            printf("Total in buffer: %d\n", counter);
            printf("Material X: %d; Material Y: %d; Material Z: %d.\n", buffer_quantity[MatX], buffer_quantity[MatY], buffer_quantity[MatZ]);
             printf("/////////////////////////////////////////////////////////////////\n");
         }
         else if(!strcmp(command, "p")) {
            sum = product_produced[ProA] + product_produced[ProB] + product_produced[ProC];
            printf("/////////////////////////////////////////////////////////////////\n");
            printf("Total produced: %ld\n", sum);
            printf("Product A (X+Y): %ld; Product B (Y+Z): %ld; Product C (Z+X): %ld.\n", product_produced[ProA], product_produced[ProB], product_produced[ProC]);
            printf("/////////////////////////////////////////////////////////////////\n");
         }
         else if(!strcmp(command, "q")) {
            sum = output_quantity[ProA] + output_quantity[ProB] + output_quantity[ProC];
            printf("/////////////////////////////////////////////////////////////////\n");
            printf("Total in output queue: %ld\n", sum);
            printf("Product A (X+Y): %ld; Product B (Y+Z): %ld; Product C (Z+X): %ld.\n", output_quantity[ProA], output_quantity[ProB], output_quantity[ProC]);
            printf("Latest product: %s\n", product_name[latest_product]);
            printf("/////////////////////////////////////////////////////////////////\n");
         }
         else if(!strcmp(command, "d")) {
            printf("/////////////////////////////////////////////////////////////////\n");
            printf("Total deadlocks: %d\n", num_deadlocks);
            printf("/////////////////////////////////////////////////////////////////\n");
         }
         else if(!strcmp(command, "h")) {
            showManual();
         }
         else if(!strcmp(command, "restart")) {
            cancelThreads();
            pthread_cancel(timerid);
            initializeStatic();
            initializeDynamic();
            pthread_create(&timerid, &attr, timer, NULL);
            createThreads();
         }
         else if(!strcmp(command, "exit")) {
            exit(0);
         }
         //test-used
         else if(!strcmp(command, "status")) {
            int s[4];
            sem_getvalue(&full, &s[0]);
            sem_getvalue(&empty, &s[1]);
            sem_getvalue(&tool, &s[2]);
            sem_getvalue(&material_available, &s[3]);
            printf("/////////////////////////////////////////////////////////////////\n");
            printf("Semaphore: full %d, empty %d, tool %d, material_available %d\n", s[0], s[1], s[2], s[3]);
            printf("Counter: %d; Free tools: %d\n", counter, free_tools);
            printf("Tool occupation: ");
            for (i = 0; i < num_operators; i++) {
               printf("No.%d: %d; ", i, btool[i]);
            }
            printf("\n");
            printf("/////////////////////////////////////////////////////////////////\n");
         }
         else {
               printf("<!> Invalid input. Try again!\n");
         }
      }
      else {
         printf("<!> Input failed :(\n");
      }
   }
   
   return 1;
}

/////////////////////////////////////////////////////////////////

