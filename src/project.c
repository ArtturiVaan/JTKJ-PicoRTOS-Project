
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

// Frequenzies (Hz) for morse buzzer.
#define DOT   700
#define DASH  700
#define SPACE 200

// Note frequencies (Hz) for startup song. S = sharp (e.g. CS5 = C#5).
#define DS4   311
#define E4    330
#define FS4   370
#define GS4   415
#define A4    440
#define B4    494
#define CS5   554
#define D5    587

// Tempo for startup song: quarter note = 200 BPM = 300 ms.
#define GAP      25
#define EIGHTH   (150 - GAP)
#define QUARTER  (300 - GAP)
#define DQUARTER (450 - GAP)
#define HALF     (600 - GAP)
#define DHALF    (900 - GAP)


// States to be used.
enum state
{
    STARTUP = 0, // Startup melody is playing.
    IDLE = 1,    // Reading data from motion sensor.
    PROCESS,     // Motion data is processed when button is pressed.
    SERIAL       // Send messages from serial monitor to device.
};

enum state programState = STARTUP;

// Motion sensor data for acceleration and gyroscope.
struct imu_data
{
    float ax;   // Acceleration x-axis.
    float ay;   // Acceleration y-axis.
    float az;   // Acceleration z-axis.
    float gx;   // Gyroscope x-axis.
    float gy;   // Gyroscope y-axis.
    float gz;   // Gyroscope z-axis.
    float t;    // Temperature.
};

struct imu_data imu_data;

char serial_buffer[BUFFER_SIZE];    // For receiving messages from serial client.

char device_buffer[BUFFER_SIZE]; // For sending messages to serial client from device.
uint8_t device_index = 0;           // Device buffer index.
uint8_t space_counter = 0;
char device_character;
uint8_t button;                     // Which button has been pressed.

static void write_to_device(const char *message)
{
    char line[CHARS_PER_LINE + 1];  // Buffer for the current line being written plus null.
    int len = strlen(message);      // Length of the whole message.
    int message_index = 0;          // Which character is being handled.
    int line_index = 0;             // How many characters in line buffer.
    int line_number = 0;            // Which line is being written.

    while (message_index < len && line_number < MAX_LINES)
    // Go through the message and for every space check how much room remains in the current line.
    // If there is no more space for a whole character, write the current line and move on to the
    // next line.
    {
        if ((message[message_index] == ' ') && (line_index > (CHARS_PER_LINE - 5)))
        // Check if there is enough space for more characters. If not, print the current line on the display.
        // Make sure the line breaks only between letters.
        {
            line[line_index] = '\0';
            write_text_xy(2, 2 + line_number * LINE_HEIGHT, line);
            line_number++;
            line_index = 0;
        }

        else if (!(message[message_index] == ' ' && line_index == 0) && line_index < CHARS_PER_LINE)
        // Copy characters in message to line buffer, except if the first character on the line is a space.
        // Make sure the length of the line does not go over the set limit.
        {
            line[line_index] = message[message_index];
            line_index++;
        }

        message_index++;
    }

    if (line_index > 0 && line_number < MAX_LINES)
    // Write all remaining characters on the last line.
    {
        line[line_index] = '\0';
        write_text_xy(2, 2 + line_number * LINE_HEIGHT, line);
    }

}

static void message_buzzer(const char *message)
    // Play sound through device buzzer when message is received from client.
{
    int len = strlen(message);  // Length of the whole message.
    uint16_t i;

    for (i = 0; i < len; i++) {
        if (message[i] == '-') {
            buzzer_play_tone(DASH, 300);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (message[i] == '.') {
            buzzer_play_tone(DOT, 100);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (message[i] == ' ') {
            vTaskDelay(pdMS_TO_TICKS(300));

        }
    }
}

static void sensor_task(void *arg)
    // Read data from IMU in IDLE state.
{
    (void)arg;

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
                &imu_data.t
            );
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void write_task(void *arg)
    // When the device is in PROCESS state, write messages to the device buffer in morse when buttons are pressed.
    // Send buffer to serial client.

    // When the device is in SERIAL state, receive message from serial client.
{
    (void)arg;

    for(;;)
    {
        // Save character to device buffer depending on device orientation and the button pressed.
        // Show character on screen and play buzzer.
        // When the device is stationary and vertical, IMU data for y-axis is 1 (treshold = 0.9), character is dash.
        // When the device is stationary and horizontal, IMU data for z-axis is 1 (treshold = 0.9), character is dot.
        if (programState == PROCESS){

            clear_display();

            if (button == BUTTON1)
            {
                // Saves character as dot. Give feedback to user.
                if (imu_data.az > 0.9){
                    buzzer_play_tone(DOT, 100);
                    write_text(".");
                    device_character = '.';
                    space_counter = 0;
                }

                // Saves character as dash. Give feedback to user.
                else if (imu_data.ay < -0.9)
                {
                    buzzer_play_tone(DASH, 300);
                    write_text("-");
                    device_character = '-';
                    space_counter = 0;
                }
            }

            if (button == BUTTON2)
            {
                // Saves character as space on first and second press. Give feedback to user.
                // Counts the number of spaces pressed in a row.
                // If the space is pressed thrice, sends the device buffer to serial client.
                space_counter++;

                if (space_counter < 3) {
                    buzzer_play_tone(SPACE, 150);
                    device_character = ' ';
                    write_text(" ");
                }

                else {
                    buzzer_play_tone(2000, 100);
                    buzzer_play_tone(4000, 100);
                    buzzer_play_tone(8000, 100);

                    // Make sure the message ends with null character.
                    device_buffer[device_index] = '\0';

                    // Print message saved in device buffer.
                    printf("%s\n",device_buffer);

                    // Reset buffer for the next message.
                    space_counter = 0;
                    device_index = 0;
                    device_character = 0;
                    for (int i = 0; i < BUFFER_SIZE; i++) {
                        device_buffer[i] = 0;
                    }
                }
            }

            if (device_character != 0 && device_index < BUFFER_SIZE - 1) {
                // Copy character to device buffer. 
                // Make sure the buffer does not overflow.
                device_buffer[device_index] = device_character;
                device_index++;

                // Make sure the last character in the buffer is null.
                device_buffer[device_index] = '\0';
            }
            programState = IDLE;
        }

        if (programState == SERIAL) {
            // Receive messages from serial client.
            clear_display();
            write_to_device(serial_buffer);
            message_buzzer(serial_buffer);
            programState = IDLE;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void read_serial_task(void *arg)
{
    // Read message from serial client when in IDLE state.
    // Change state to SERIAL.

    (void)arg;

    int serial_index = 0;

    for(;;)
    {
        if (programState == IDLE)
        {
            int serial_character = getchar_timeout_us(0);

            if (serial_character >= 0)
            {
                if (serial_character == '\n') {
                    // Change state to SERIAL and send message with newline.
                    serial_buffer[serial_index] = '\0';
                    programState = SERIAL;
                    serial_index = 0;
                }

                else if (serial_index < BUFFER_SIZE - 1) {
                    // Collect characters to serial buffer
                    serial_buffer[serial_index++] = (char)serial_character;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void startup_task(void *arg)
{
    // Play the startup song once, then let other tasks start working.
    
    (void)arg;

    // Pickup
    buzzer_play_tone(E4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));

    // Bar 1
    buzzer_play_tone(A4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(A4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(CS5, QUARTER);  vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(CS5, QUARTER);  vTaskDelay(pdMS_TO_TICKS(GAP));

    // Bar 2
    buzzer_play_tone(A4, HALF);      vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(E4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(E4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));

    // Bar 3
    buzzer_play_tone(E4, DQUARTER);  vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(E4, EIGHTH);    vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(B4, EIGHTH);    vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(A4, EIGHTH);    vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(GS4, EIGHTH);   vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(FS4, EIGHTH);   vTaskDelay(pdMS_TO_TICKS(GAP));

    // Bar 4
    buzzer_play_tone(E4, DHALF);     vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(E4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));


    // Bar 5
    buzzer_play_tone(A4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(A4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(CS5, QUARTER);  vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(CS5, QUARTER);  vTaskDelay(pdMS_TO_TICKS(GAP));

    // Bar 6
    buzzer_play_tone(A4, HALF);      vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(E4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(A4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));

    // Bar 7
    buzzer_play_tone(GS4, QUARTER);  vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(FS4, EIGHTH);   vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(GS4, EIGHTH);   vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(A4, QUARTER);   vTaskDelay(pdMS_TO_TICKS(GAP));
    buzzer_play_tone(DS4, QUARTER);  vTaskDelay(pdMS_TO_TICKS(GAP));

    // Bar 8
    buzzer_play_tone(E4, DHALF);     vTaskDelay(pdMS_TO_TICKS(GAP));

    printf("Device ready\n");
    programState = IDLE;    // Device is ready to be used.
    vTaskDelete(NULL);      // This task is not needed anymore.
}

static void button_interrupt(uint gpio, uint32_t eventMask)
{
    // Change state to PROCESS when button is pressed.
    if (programState == IDLE){
        button = gpio;
        programState = PROCESS;
    }
}

int main()
{
    // Initialize GPIO, SDK, display and buzzer.
    stdio_init_all();

    gpio_init(BUTTON1);
    gpio_set_dir(BUTTON1, GPIO_IN);

    gpio_init(BUTTON2);
    gpio_set_dir(BUTTON2, GPIO_IN);

    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, button_interrupt);
    gpio_set_irq_enabled(BUTTON2, GPIO_IRQ_EDGE_FALL, true);

    // Wait until the serial monitor is connected.
    while (!stdio_usb_connected())
    {
        sleep_ms(10);
    }
    init_hat_sdk();
    sleep_ms(300); // Wait some time so initialization of USB and hat is done.

    init_display();
    init_buzzer();

    init_ICM42670();
    ICM42670_start_with_default_values();

    TaskHandle_t hSensorTask, hWriteTask, hReadSerialTask, hStartUpTask;

    // Create the tasks with xTaskCreate
    BaseType_t result = xTaskCreate(sensor_task,        // Task function
                                    "sensor",           // Name of the task
                                    DEFAULT_STACK_SIZE, // Size of the stack for this task (in words). Generally 1024 or 2048
                                    NULL,               // Arguments of the task
                                    2,                  // Priority of this task
                                    &hSensorTask);      // A handle to control the execution of this task

    if (result != pdPASS)
    {
        printf("Example Task creation failed\n");
        return 0;
    }

    BaseType_t result1 = xTaskCreate(write_task,         // Task function
                                     "write",            // Name of the task
                                     DEFAULT_STACK_SIZE, // Size of the stack for this task (in words). Generally 1024 or 2048
                                     NULL,               // Arguments of the task
                                     2,                  // Priority of this task
                                     &hWriteTask);       // A handle to control the execution of this task

    if (result1 != pdPASS)
    {
        printf("Example Task creation failed\n");
        return 0;
    }

    BaseType_t result2 = xTaskCreate(read_serial_task,   // Task function
                                    "read_serial",       // Name of the task
                                    DEFAULT_STACK_SIZE,  // Size of the stack for this task (in words). Generally 1024 or 2048
                                    NULL,                // Arguments of the task
                                    2,                   // Priority of this task
                                    &hReadSerialTask);   // A handle to control the execution of this task

    if (result2 != pdPASS)
    {
        printf("Example Task creation failed\n");
        return 0;
    }

        BaseType_t result3 = xTaskCreate(startup_task,   // Task function
                                    "startup",       // Name of the task
                                    DEFAULT_STACK_SIZE,  // Size of the stack for this task (in words). Generally 1024 or 2048
                                    NULL,                // Arguments of the task
                                    2,                   // Priority of this task
                                    &hStartUpTask);   // A handle to control the execution of this task

    if (result3 != pdPASS)
    {
        printf("Example Task creation failed\n");
        return 0;
    }

    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}
