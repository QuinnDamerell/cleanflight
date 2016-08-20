/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#define SRC_MAIN_SCHEDULER_C_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "platform.h"

#include "scheduler/scheduler.h"
#include "build/debug.h"
#include "build/build_config.h"

#include "common/maths.h"

#include "drivers/system.h"
#include "config/config_unittest.h"

// The current task that is executing. Used to determin what the
// SELF_TASK is.
static cfTask_t *currentTask = NULL;

// The realtime guard ensures we are idle when the next realtime task should 
// run. At runtime the system finds the longest running non-realtime task and 
// sets it's average run time as the guard time clamped by the min and max values.
#define REALTIME_GUARD_INTERVAL_MIN     10
#define REALTIME_GUARD_INTERVAL_MAX     300
#define REALTIME_GUARD_INTERVAL_MARGIN  25
static uint32_t realtimeGuardInterval = REALTIME_GUARD_INTERVAL_MAX;

// Holds the current time - this is messy but its used by other files.
uint32_t currentTime = 0;

// Vars to keep track of the work load.
static uint32_t currentSchedulerExecutionPasses;
static uint32_t currentSchedulerExecutionPassesWithWork;
uint16_t averageSystemLoadPercent = 0;

// The system task exectuion funtion. This does some calculation work.
void taskSystem(void)
{
    // Calc the current cpu work load.
    if (currentSchedulerExecutionPasses > 0) {
        averageSystemLoadPercent = 100 * currentSchedulerExecutionPassesWithWork / currentSchedulerExecutionPasses;
        currentSchedulerExecutionPasses = 0;
        currentSchedulerExecutionPassesWithWork = 0;
    }

    // Calculate guard interval, find the longest running task and set the guard to it's time.
    uint32_t maxNonRealtimeTaskTime = 0;
    for (uint16_t ii = 0; ii < taskCount; ii++) {
        cfTask_t *task = &cfTasks[ii];
        // Todo, ideal priorites can really throw this out of wack. We might want to account for them.
        if (task->priority != TASK_PRIORITY_REALTIME) {
            maxNonRealtimeTaskTime = MAX(maxNonRealtimeTaskTime, task->averageExecutionTime);
        }
    }
    // Clamp by the min, max, and always add the margin.
    realtimeGuardInterval = constrain(maxNonRealtimeTaskTime, REALTIME_GUARD_INTERVAL_MIN, REALTIME_GUARD_INTERVAL_MAX) + REALTIME_GUARD_INTERVAL_MARGIN;

#if defined SCHEDULER_DEBUG
    debug[2] = realtimeGuardInterval;
#endif
}

#ifndef SKIP_TASK_STATISTICS
void getTaskInfo(const int taskId, cfTaskInfo_t * taskInfo)
{
    taskInfo->taskName = cfTasks[taskId].taskName;
    taskInfo->isEnabled = cfTasks[taskId].isEnabled;
    taskInfo->desiredPeriod = cfTasks[taskId].desiredPeriod;
    taskInfo->priority = cfTasks[taskId].priority;
    taskInfo->maxExecutionTime = cfTasks[taskId].maxExecutionTime;
    taskInfo->totalExecutionTime = cfTasks[taskId].totalExecutionTime;
    taskInfo->averageExecutionTime = cfTasks[taskId].averageExecutionTime;
    taskInfo->latestDeltaTime = cfTasks[taskId].taskLatestDeltaTime;
}
#endif

void updateTaskExecutionPeriod(const int taskId, uint32_t newPeriodMicros)
{
    if ((taskId == TASK_SELF && currentTask != NULL) || (taskId < (int)taskCount && taskId >= 0)) {
        cfTask_t *task = taskId == TASK_SELF ? currentTask : &cfTasks[taskId];
        task->desiredPeriod = MAX(100, newPeriodMicros);  // Limit delay to 100us (10 kHz) to prevent scheduler clogging
    }
}

void setTaskEnabled(const int taskId, bool enabled)
{
    if ((taskId == TASK_SELF && currentTask != NULL) || (taskId < (int)taskCount && taskId >= 0)) {
        cfTask_t *task = taskId == TASK_SELF ? currentTask : &cfTasks[taskId];
        task->isEnabled = (enabled && task->taskFunc);
    }
}

uint32_t getTaskDeltaTime(const int taskId)
{
    if ((taskId == TASK_SELF && currentTask != NULL) || (taskId < (int)taskCount && taskId >= 0)) {
        cfTask_t *task = taskId == TASK_SELF ? currentTask : &cfTasks[taskId];
        return task->taskLatestDeltaTime;
    } else {
        return 0;
    }
}

void schedulerInit(void)
{
    // Disable all tasks and set defaults
    for(uint16_t ii = 0; ii < taskCount; ii++)
    {
        cfTasks[ii].isEnabled = false;
        cfTasks[ii].isWaitingToBeRan = false;
        cfTasks[ii].lastIdealExecutionTime = 0;
    }
}

void schedulerExecute(void)
{
    // Cache currentTime
    currentTime = micros();

    // Check for realtime tasks and when they need to run next.
    uint32_t timeToNextRealtimeTask = UINT32_MAX;
    for (uint16_t ii = 0; ii < taskCount; ii++) {
        cfTask_t *task = &cfTasks[ii];
        if(task->isEnabled && task->priority >= TASK_PRIORITY_REALTIME) {
            const uint32_t nextExecuteAt = task->lastExecutedAt + task->desiredPeriod;
            if ((int32_t)(currentTime - nextExecuteAt) >= 0) {
                timeToNextRealtimeTask = 0;
            } else {
                const uint32_t newTimeInterval = nextExecuteAt - currentTime;
                timeToNextRealtimeTask = MIN(timeToNextRealtimeTask, newTimeInterval);
            }
        }
    }

    // Determin if we are in the realtime guard time or not. If so we won't schedule
    // and tasks that aren't realtime.
    const bool outsideRealtimeGuardInterval = (timeToNextRealtimeTask > realtimeGuardInterval);

    // The task to be invoked
    cfTask_t *selectedTask = NULL;
    uint16_t selectedTaskStarvationPriority = 0;

    // Loop through all of the tasks. Check if any of them need to execute now and update 
    // the dynamicPriority.
    for (uint16_t ii = 0; ii < taskCount; ii++) {
        cfTask_t *task = &cfTasks[ii];

        // If the task isn't enabled skip all of this. 
        if(!task->isEnabled) {
            continue;
        }

        // Check if we aren't waiting to be ran but we should be.
        if(!task->isWaitingToBeRan) {
            // Check if this is an event driven task.
            if (task->checkFunc != NULL) {
                // Ask if we should run this task.
                if(task->checkFunc(currentTime - task->lastExecutedAt))
                {
                    // We should run this task, set the ideal execution time to now.
                    task->lastIdealExecutionTime = currentTime;
                    task->isWaitingToBeRan = true;
                }
            }
            else
            {
                // This isn't event driven, see if it should be ran based on time. 
                // Note it is important to use the time it should have been schedulled not when it was,
                // this will give use more accurate interval times.
                // TODO: handle currentTime rolling over.
                if((task->lastIdealExecutionTime + task->desiredPeriod) <= currentTime)
                {
                    // Ensure that our next scheduled time (after this one) is past the current time
                    // so we don't starve on really aggressive tasks.
                    while((task->lastIdealExecutionTime + task->desiredPeriod) <= currentTime)
                    {
                        task->lastIdealExecutionTime += task->desiredPeriod;
                    }
                    task->isWaitingToBeRan = true;
                }
            }
        }

        // Now check if the task is waiting to be ran. 
        if(task->isWaitingToBeRan) {

            // Figure out how many cycles this task has been waiting.
            uint32_t taskAge = 1 + ((currentTime - task->lastIdealExecutionTime) / task->desiredPeriod);

            // Figure out the starvationPriority. This will get higher the longer the task waits.  
            // Note that for idle task the pri is 0 so they always fall to 1. They will always be overtaken by
            // any other task.          
            uint32_t starvationPriority = 1 + task->priority * taskAge;

            // Now, figure out if we should select this task 
            if (starvationPriority > selectedTaskStarvationPriority) {
                const bool taskCanBeChosenForScheduling =
                    (outsideRealtimeGuardInterval) ||
                    (task->priority == TASK_PRIORITY_REALTIME);
                if (taskCanBeChosenForScheduling) {
                    selectedTaskStarvationPriority = starvationPriority;
                    selectedTask = task;
                }
            }
        }   
    }

    // Set the current task, note this can be null.
    currentTask = selectedTask;
    
    // Update our load values. 
    currentSchedulerExecutionPasses++;
    if(currentTask != NULL)
    {
        currentSchedulerExecutionPassesWithWork++;
    }

    if (selectedTask != NULL) {
        // Found a task that should be run
        selectedTask->taskLatestDeltaTime = currentTime - selectedTask->lastExecutedAt;
        selectedTask->lastExecutedAt = currentTime;        

        // Execute task
        const uint32_t currentTimeBeforeTaskCall = micros();
        selectedTask->taskFunc();
        const uint32_t taskExecutionTime = micros() - currentTimeBeforeTaskCall;

        // Clear our current task 
        currentTask = NULL;
        selectedTask->isWaitingToBeRan = false;

        selectedTask->averageExecutionTime = ((uint32_t)selectedTask->averageExecutionTime * 31 + taskExecutionTime) / 32;
#ifndef SKIP_TASK_STATISTICS
        selectedTask->totalExecutionTime += taskExecutionTime;   // time consumed by scheduler + task
        selectedTask->maxExecutionTime = MAX(selectedTask->maxExecutionTime, taskExecutionTime);
#endif
#if defined SCHEDULER_DEBUG
        debug[3] = (micros() - currentTime) - taskExecutionTime;
    } else {
        debug[3] = (micros() - currentTime);
#endif
    }
    GET_SCHEDULER_LOCALS();
}
