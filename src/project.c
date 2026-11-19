
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

// Add here necessary states
enum state
{
    IDLE = 1,
    DATA_READY,
    DISPLAY,
    SERIAL
};
enum state programState = IDLE;

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

static void write_wrapped(const char *text)
{
    char line[CHARS_PER_LINE + 1];
    int len = strlen(text);
    int i = 0;
    int row = 0;

    while (i < len && row < MAX_LINES)
    {
        int n = CHARS_PER_LINE;

        if (len - i > CHARS_PER_LINE)
        {
            int b = CHARS_PER_LINE;
            while (b > 0 && text[i + b] != ' ')
            {
                b--;
            }
            if (b > 0)
            {
                n = b;
            }
        }

        strncpy(line, text + i, n);
        line[n] = '\0';
        write_text_xy(2, 2 + row * LINE_HEIGHT, line);

        i += n;
        while (text[i] == ' ')
        {
            i++;
        }
        row++;
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
                &imu_data.ax, // imu_data.ax
                &imu_data.ay,
                &imu_data.az,
                &imu_data.gx,
                &imu_data.gy,
                &imu_data.gz,
                &imu_data.t);
        }

        // tight_loop_contents(); // Modify with application code here.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

uint8_t morse_buf[BUFFER_SIZE];
char character;
uint8_t morse_index = 0;
uint8_t space_counter = 0;
uint8_t button;
char serial_buffer[BUFFER_SIZE];

static void write_task(void *arg)
{

    (void)arg;

    for(;;)
    {
        if (programState == DATA_READY){

            clear_display();

            if (button == BUTTON1)
            {
                // piste vai viiva
                if (imu_data.az > 0.9){
                    character = '.';
                    write_text(".");
                }

                if (imu_data.ay < -0.9)
                {
                    character = '-';
                    write_text("-");
                }

                space_counter = 0;
            }

            if (button == BUTTON2)
            {
                // välilyönti
                character = ' ';
                write_text(" ");
                space_counter++;


                if (space_counter > 2)
                {
                    printf("sent: %s\n",morse_buf);
                    space_counter = 0;
                    morse_index = 0;
                    for (int i = 0; i < BUFFER_SIZE; i++)
                    {
                        morse_buf[i] = 0;
                    }
                }
            }

            if (morse_index < BUFFER_SIZE - 1)
            {                                       // tarkistetaan ettei buffer ylity
                morse_buf[morse_index] = character; // lisätään merkki indeksiin n
                morse_index++;
            }
            programState = IDLE;
        }

        if (programState == SERIAL){
            clear_display();
            write_wrapped(serial_buffer);
            programState = IDLE;

        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void serial_task(void *arg)
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
            if (ch == '\n')
            {
                serial_buffer[index] = '\0';

                programState = SERIAL;

                index = 0;
            }
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
    /*
    static TickType_t lastPressTime = 0;
    TickType_t currentTime = xTaskGetTickCountFromISR();

    if (lastPressTime != 0 && (currentTime - lastPressTime) < pdMS_TO_TICKS(100))
    {
        return;
    }

    lastPressTime = currentTime;
*/
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
