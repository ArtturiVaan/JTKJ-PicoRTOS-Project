
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"

// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048 

//Add here necessary states
enum state { IDLE=1, DATA_READY, DISPLAY };
enum state programState = IDLE;

float motionData[7];

static void sensor_task(void *arg){
    (void)arg;

    init_ICM42670();
    ICM42670_start_with_default_values();

    for(;;){

        if (programState == IDLE) {
            ICM42670_read_sensor_data(
            &motionData[0],
            &motionData[1],
            &motionData[2],
            &motionData[3],
            &motionData[4],
            &motionData[5],
            &motionData[6]
            );
        
            programState = DATA_READY;
        }

        tight_loop_contents(); // Modify with application code here.
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void processor_task(void *arg){
    (void)arg;

    for(;;){

        if (programState == DATA_READY) {
            float gyro_x = motionData[3];
            float gyro_y = motionData[4];
            float gyro_z = motionData[5];
            printf("x akseli: %f, y akseli: %f, z akseli: %f\n",
            gyro_x, gyro_y, gyro_z);
            programState = IDLE;
        }

        tight_loop_contents(); // Modify with application code here.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main() {
    stdio_init_all();
    // Uncomment this lines if you want to wait till the serial monitor is connected
    while (!stdio_usb_connected()){
        sleep_ms(10);
    } 
    init_hat_sdk();
    sleep_ms(300); //Wait some time so initialization of USB and hat is done.

    TaskHandle_t hSensorTask, hProcessorTask;

    // Create the tasks with xTaskCreate
    BaseType_t result = xTaskCreate(sensor_task,       // (en) Task function
                "sensor",              // (en) Name of the task 
                DEFAULT_STACK_SIZE, // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,               // (en) Arguments of the task 
                2,                  // (en) Priority of this task
                &hSensorTask);    // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Example Task creation failed\n");
        return 0;
    }

    // Create the tasks with xTaskCreate
    BaseType_t result1 = xTaskCreate(processor_task,       // (en) Task function
                "processor",              // (en) Name of the task 
                DEFAULT_STACK_SIZE, // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,               // (en) Arguments of the task 
                2,                  // (en) Priority of this task
                &hProcessorTask);    // (en) A handle to control the execution of this task

    if(result1 != pdPASS) {
        printf("Example Task creation failed\n");
        return 0;
    }


    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}

