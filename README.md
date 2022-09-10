# etask_ota

This package contains the code to perform an OTA update from an S3 repository.
It also provides a FreeRTOS task `etask_ota()`, which can run as part of your project.
This task will check for an update every 6 hours by default.

Include this repository as a submodule in your project, or just download as a library.

## Example excerpt

~~~c
#include <etask_ota.h>

TaskHandle_t ota_task;

void setup() {
  ...
  // Create a task to check and get OTA updates from S3
  xTaskCreate(
    etask_ota,
    "OTA_Task",
    5000,
    NULL,
    0,
    &ota_task
  );
}
~~~
