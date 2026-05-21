#pragma once
#include "Arduino.h"
#include "config.h"

unsigned long prevIdle0Runtime, prevIdle1Runtime, prevRuntime;
float cpuUsage0, cpuUsage1;

void init_mon() {
    xTaskCreatePinnedToCore([](void* _) {
        uint32_t totalRunTime;
        unsigned int orgTaskCount = uxTaskGetNumberOfTasks();
        TaskStatus_t* taskStatusArray = (TaskStatus_t*)malloc(orgTaskCount * sizeof(TaskStatus_t));
        while (1) {
            const UBaseType_t taskCount = uxTaskGetNumberOfTasks();
            if (taskCount != orgTaskCount) {
                orgTaskCount = taskCount;
                free(taskStatusArray);
                taskStatusArray = (TaskStatus_t*)malloc(orgTaskCount * sizeof(TaskStatus_t));
            }

            totalRunTime = portGET_RUN_TIME_COUNTER_VALUE();
            uxTaskGetSystemState(taskStatusArray, taskCount, &totalRunTime);

            for (UBaseType_t i = 0; i < taskCount; i++) {
                if (strcmp(taskStatusArray[i].pcTaskName, "IDLE0") == 0) {
                    cpuUsage0 = totalRunTime ? fabsf(100.0f - ((taskStatusArray[i].ulRunTimeCounter - prevIdle0Runtime) / (float)(totalRunTime - prevRuntime)) * 100.0f) : 0;
                    prevIdle0Runtime = taskStatusArray[i].ulRunTimeCounter;
                }
                if (strcmp(taskStatusArray[i].pcTaskName, "IDLE1") == 0) {
                    cpuUsage1 = totalRunTime ? fabsf(100.0f - ((taskStatusArray[i].ulRunTimeCounter - prevIdle1Runtime) / (float)(totalRunTime - prevRuntime)) * 100.0f) : 0;
                    prevIdle1Runtime = taskStatusArray[i].ulRunTimeCounter;
                }
            }

            prevRuntime = totalRunTime;
            delay(1000);
        }
    }, "mon dude", MONITOR_TASK_STACK_SIZE, NULL, 3, NULL, MONITOR_TASK_CORE);
}