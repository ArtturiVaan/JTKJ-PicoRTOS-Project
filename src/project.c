
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"

// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048
#define BUFFER_SIZE 256

// How many characters can fit horizontally on the screen, what is the height difference between lines
// and how many lines can fit vertically on the screen.
#define CHARS_PER_LINE 20
#define LINE_HEIGHT 10
#define MAX_LINES 6


// States to be used.
enum state
{
    IDLE = 1,
    DATA_READY,
    DISPLAY,
    SERIAL
};
enum state programState = IDLE;

// Motion sensor data for acceleration and gyroscope.
struct imu_data
{
    float ax;
    float ay;
    float az;
    float gx;
    float gy;
    float gz;
    float t;
};

struct imu_data imu_data;

static void write_message(const char *message)
{
    char line[CHARS_PER_LINE + 1]; // buffer for the current line being written
    int len = strlen(message); // length of the whole message
    int index = 0; // which character is being handled
    int line_index = 0; // how many characters in line buffer
    int row = 0; // which row is being written

    // Go through the message and for every space check how much room remains in the current line.
    // If there is no more space for a whole character, write the current line and move on to the
    // next line.
    while (index < len && row < MAX_LINES)
    {
        if (message[index] == ' ' && line_index > (CHARS_PER_LINE - 5))
        {
            line[line_index] = '\0';
            write_text_xy(2, 2 + row * LINE_HEIGHT, line);
            row++;
            line_index = 0;

        }

        // Do not include space on the next line.
        else if (!(message[index] == ' ' && line_index == 0) && line_index < CHARS_PER_LINE)
        {
            line[line_index] = message[index];
            line_index++;
        }

        index++;

    }

    // Write all remaining characters
    if (line_index > 0 && row < MAX_LINES)
    {
        line[line_index] = '\0';
        write_text_xy(2, 2 + row * LINE_HEIGHT, line);
    }

}

static void sensor_task(void *arg)
{
    (void)arg;

    init_ICM42670();
    ICM42670_start_with_default_values();

    for (;;)
    {

        if (programState == IDLE)
        {
            ICM42670_read_sensor_data(
                &imu_data.ax,
                &imu_data.ay,
                &imu_data.az,
                &imu_data.gx,
                &imu_data.gy,
                &imu_data.gz,
                &imu_data.t);
        }

        // tight_loop_contents();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

char serial_buffer[BUFFER_SIZE];
uint8_t morse_buffer[BUFFER_SIZE];

char character;
uint8_t morse_index = 0;
uint8_t space_counter = 0;
uint8_t button;

static void write_task(void *arg)
{
    (void)arg;

    for(;;)
    {
        // Send messages to serial client.
        if (programState == DATA_READY){

            clear_display();

            if (button == BUTTON1)
            {
                // Show dot on screen.
                if (imu_data.az > 0.9){
                    buzzer_play_tone(1000, 100);
                    write_text(".");
                    character = '.';
                }

                // Show dash on screen.
                if (imu_data.ay < -0.9)
                {
                    buzzer_play_tone(1000, 300);
                    write_text("-");
                    character = '-';
                }

                space_counter = 0;
            }

            if (button == BUTTON2)
            {
                // Show space on screen (empty).
                space_counter++;

                if ((space_counter == 1) || (space_counter == 2))
                {
                    buzzer_play_tone(200, 150);
                }
                
                character = ' ';
                write_text(" ");

                // Print morse buffer when there are three spaces in a row.
                if (space_counter > 2)
                {
                    buzzer_play_tone(2000, 100);
                    buzzer_play_tone(4000, 100);
                    buzzer_play_tone(8000, 100);
                    printf("sent: %s\n",morse_buffer);
                    space_counter = 0;
                    morse_index = 0;
                    character = 0;
                    for (int i = 0; i < BUFFER_SIZE; i++)
                    {
                        morse_buffer[i] = 0;
                    }
                }
            }

            // Add character to morse buffer. 
            // Make sure the buffer does not overflow.
            if (character != 0 && morse_index < BUFFER_SIZE - 1)
            {
                morse_buffer[morse_index] = character;
                morse_index++;
            }
            programState = IDLE;
        }

        // Receive messages from serial client.
        if (programState == SERIAL){
            clear_display();
            write_message(serial_buffer);
            programState = IDLE;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void serial_task(void *arg)
// Add characters to serial buffer and change state to SERIAL when
// the message is ready to be sent.
{
    (void)arg;

    int index = 0;

    for(;;)
    {
        if (programState == IDLE)
        {

            int ch = getchar_timeout_us(0);

        if (ch >= 0)
        {
            // Send message.
            if (ch == '\n')
            {
                serial_buffer[index] = '\0';
                programState = SERIAL;
                index = 0;
            }

            // Collect characters to serial buffer
            else if (index < BUFFER_SIZE - 1)
            {
                serial_buffer[index++] = (char)ch;
            }
        }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


static void button_interrupt(uint gpio, uint32_t eventMask)
{
    // Change state to DATA_READY when button is pressed.
    if (programState == IDLE){
        button = gpio;
        programState = DATA_READY;
    }
}

int main()
{
    stdio_init_all();

    gpio_init(BUTTON1);
    gpio_set_dir(BUTTON1, GPIO_IN);

    gpio_init(BUTTON2);
    gpio_set_dir(BUTTON2, GPIO_IN);

    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, button_interrupt);
    gpio_set_irq_enabled(BUTTON2, GPIO_IRQ_EDGE_FALL, true);

    // Uncomment this lines if you want to wait till the serial monitor is connected
    while (!stdio_usb_connected())
    {
        sleep_ms(10);
    }
    init_hat_sdk();
    sleep_ms(300); // Wait some time so initialization of USB and hat is done.

    init_display();

    init_buzzer();

    TaskHandle_t hSensorTask, hWriteTask, hSerialTask;

    // Create the tasks with xTaskCreate
    BaseType_t result = xTaskCreate(sensor_task,        // (en) Task function
                                    "sensor",           // (en) Name of the task
                                    DEFAULT_STACK_SIZE, // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                                    NULL,               // (en) Arguments of the task
                                    2,                  // (en) Priority of this task
                                    &hSensorTask);      // (en) A handle to control the execution of this task

    if (result != pdPASS)
    {
        printf("Example Task creation failed\n");
        return 0;
    }

    // Create the tasks with xTaskCreate
    BaseType_t result1 = xTaskCreate(write_task,         // (en) Task function
                                     "write",            // (en) Name of the task
                                     DEFAULT_STACK_SIZE, // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                                     NULL,               // (en) Arguments of the task
                                     2,                  // (en) Priority of this task
                                     &hWriteTask);       // (en) A handle to control the execution of this task

    if (result1 != pdPASS)
    {
        printf("Example Task creation failed\n");
        return 0;
    }

        // Create the tasks with xTaskCreate
    BaseType_t result2 = xTaskCreate(serial_task,        // (en) Task function
                                    "serial",           // (en) Name of the task
                                    DEFAULT_STACK_SIZE, // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                                    NULL,               // (en) Arguments of the task
                                    2,                  // (en) Priority of this task
                                    &hSerialTask);      // (en) A handle to control the execution of this task

    if (result2 != pdPASS)
    {
        printf("Example Task creation failed\n");
        return 0;
    }

    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}
