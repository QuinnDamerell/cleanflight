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

#include <stdint.h>

extern "C" {
    #include "platform.h"
    #include "fc/fc_tasks.h"
    #include "scheduler/scheduler.h"
}

#include "unittest_macros.h"
#include "gtest/gtest.h"
enum {
    systemTime = 10,
    pidLoopCheckerTime = 50,
    updateAccelerometerTime = 200,
    handleSerialTime = 30,
    updateBeeperTime = 1,
    updateBatteryTime = 2,
    updateRxCheckTime = 34,
    updateRxMainTime = 12,
    processGPSTime = 13,
    updateCompassTime = 195,
    updateBaroTime = 201,
    updateSonarTime = 14,
    calculateAltitudeTime = 154,
    updateDisplayTime = 15,
    telemetryTime = 16,
    ledStripTime = 17,
    transponderTime = 18
};

 extern "C" {
    uint32_t simulatedTime = 0;
    bool rxCheckReturnValue = false;
    uint32_t micros(void) {return simulatedTime;}
    void taskMainPidLoopChecker(void) {simulatedTime+=pidLoopCheckerTime;}
    void taskUpdateAccelerometer(void) {simulatedTime+=updateAccelerometerTime;}
    void taskHandleSerial(void) {simulatedTime+=handleSerialTime;}
    void taskUpdateBeeper(void) {simulatedTime+=updateBeeperTime;}
    void taskUpdateBattery(void) {simulatedTime+=updateBatteryTime;}
    bool taskUpdateRxCheck(uint32_t currentDeltaTime) {UNUSED(currentDeltaTime);simulatedTime+=updateRxCheckTime;return rxCheckReturnValue;}
    void taskUpdateRxMain(void) {simulatedTime+=updateRxMainTime;}
    void taskProcessGPS(void) {simulatedTime+=processGPSTime;}
    void taskUpdateCompass(void) {simulatedTime+=updateCompassTime;}
    void taskUpdateBaro(void) {simulatedTime+=updateBaroTime;}
    void taskUpdateSonar(void) {simulatedTime+=updateSonarTime;}
    void taskCalculateAltitude(void) {simulatedTime+=calculateAltitudeTime;}
    void taskUpdateDisplay(void) {simulatedTime+=updateDisplayTime;}
    void taskTelemetry(void) {simulatedTime+=telemetryTime;}
    void taskLedStrip(void) {simulatedTime+=ledStripTime;}
    void taskTransponder(void) {simulatedTime+=transponderTime;}
 }

TEST(SchedulerUnittest, TestPriorites)
{
    EXPECT_EQ(14, taskCount);
    // if any of these fail then task priorities have changed and ordering in TestQueue needs to be re-checked
    EXPECT_EQ(TASK_PRIORITY_HIGH, cfTasks[TASK_SYSTEM].priority);
    EXPECT_EQ(TASK_PRIORITY_REALTIME, cfTasks[TASK_GYROPID].priority);
    EXPECT_EQ(TASK_PRIORITY_MEDIUM, cfTasks[TASK_ACCEL].priority);
    EXPECT_EQ(TASK_PRIORITY_LOW, cfTasks[TASK_SERIAL].priority);
    EXPECT_EQ(TASK_PRIORITY_MEDIUM, cfTasks[TASK_BATTERY].priority);
    EXPECT_EQ(TASK_PRIORITY_IDLE, cfTasks[TASK_TELEMETRY].priority);
}

// Inits the scheduler
TEST(SchedulerUnittest, InitTest)
{
    // Make sure we have some tasks
    EXPECT_GT(taskCount, 13);

    // Try to init the scheduler 
    schedulerInit();

    // Make sure all of the tasks are disabled
    for(uint16_t ii = 0; ii < taskCount; ii++)
    {
        EXPECT_FALSE(cfTasks[ii].isEnabled);
        EXPECT_EQ(cfTasks[ii].lastIdealExecutionTime, 0);
    }    
}

// Disables and Enables tasks.
TEST(SchedulerUnittest, EnableDisableTest)
{
    // Make sure we have some tasks
    EXPECT_GT(taskCount, 13);

    // Try to init the scheduler 
    schedulerInit();

    // Make sure it's off 
    EXPECT_FALSE(cfTasks[TASK_SYSTEM].isEnabled);
    EXPECT_FALSE(cfTasks[TASK_GYROPID].isEnabled);

    // Enable one
    setTaskEnabled(TASK_SYSTEM, true);

    // Check 
    EXPECT_TRUE(cfTasks[TASK_SYSTEM].isEnabled);
    EXPECT_FALSE(cfTasks[TASK_GYROPID].isEnabled);

    // Enable two
    setTaskEnabled(TASK_GYROPID, true);

    // Check 
    EXPECT_TRUE(cfTasks[TASK_SYSTEM].isEnabled);
    EXPECT_TRUE(cfTasks[TASK_GYROPID].isEnabled);

    // Disable them
    setTaskEnabled(TASK_SYSTEM, false);
    setTaskEnabled(-1, false);
    setTaskEnabled(9999, false);    

    // Check 
    EXPECT_FALSE(cfTasks[TASK_SYSTEM].isEnabled);
    EXPECT_TRUE(cfTasks[TASK_GYROPID].isEnabled);
}

// Does an empty schedule
TEST(SchedulerUnittest, DriveEmptySchedule)
{
    // Try to init the scheduler 
    schedulerInit();

    // Ensure the system time is 0
    EXPECT_EQ(simulatedTime, 0);

    // Run the scheduler
    schedulerExecute();

    // If anything fired the time would be greater than 0
    EXPECT_EQ(simulatedTime, 0);

    // Update the time and run Run the scheduler
    simulatedTime += 30000;
    schedulerExecute();

    // If anything fired the time would be greater than 0
    EXPECT_EQ(simulatedTime, 30000);

     // Run the scheduler
    simulatedTime += 3000000;     
    schedulerExecute();
    schedulerExecute();
    schedulerExecute();

    // If anything fired the time would be greater than 0
    EXPECT_EQ(simulatedTime, 3030000);
}

TEST(SchedulerUnittest, BasicTask)
{
    simulatedTime = 0;
    uint32_t expectedTime = 0;

    // Try to init the scheduler 
    schedulerInit();

    // Enable the system task 
    setTaskEnabled(TASK_ACCEL, true);
    updateTaskExecutionPeriod(TASK_ACCEL, 30000);

    // Run the scheduler, this shouldn't do anything
    expectedTime = simulatedTime = 29999;
    schedulerExecute();
    EXPECT_EQ(simulatedTime, expectedTime);    

    // Now update the time and make sure we are called. 
    expectedTime = simulatedTime = 30000;
    schedulerExecute();
    expectedTime += updateAccelerometerTime;
    EXPECT_EQ(simulatedTime, expectedTime); 

    // Run it again and make sure it doesn't fire
    expectedTime = simulatedTime = 40000;
    schedulerExecute();
    EXPECT_EQ(simulatedTime, expectedTime); 

    // Now set the time past when it should have ran and make sure it does. 
    // It should run at 60000, so set the time to 70000
    expectedTime = simulatedTime = 70000;
    schedulerExecute();
    expectedTime += updateAccelerometerTime;    
    EXPECT_EQ(simulatedTime, expectedTime); 

    // Now set the time to be just before the next fire.
    // Note since we didn't fire exactly at 60000 this should still fire exactly at 90000
    expectedTime = simulatedTime = 89999;
    schedulerExecute();
    EXPECT_EQ(simulatedTime, expectedTime); 

    // Now make sure it fires at 90000
    expectedTime = simulatedTime = 90000;
    schedulerExecute();
    expectedTime += updateAccelerometerTime;  
    EXPECT_EQ(simulatedTime, expectedTime); 

    // Disable it and make sure it doesn't fire
    setTaskEnabled(TASK_ACCEL, false);
    expectedTime = simulatedTime = 500000;
    schedulerExecute();
    EXPECT_EQ(simulatedTime, expectedTime); 
}

TEST(SchedulerUnittest, BasicEventTask)
{
    simulatedTime = 0;
    uint32_t expectedTime = 0;
    schedulerInit();

    // Setup the RX task as our testing task, this task uses a 
    // event based timer.
    setTaskEnabled(TASK_RX, true);

    // Setup for the first call
    rxCheckReturnValue = false;
    expectedTime += updateRxCheckTime;
    schedulerExecute();

    // We should see a small time bump from the check function.
    // But not a big bump from the check and the task running.
    EXPECT_EQ(simulatedTime, expectedTime);

    // Do it once again.
    expectedTime += updateRxCheckTime;
    schedulerExecute();
    EXPECT_EQ(simulatedTime, expectedTime);

    // Now enable the callback and see if we get the task call.
    rxCheckReturnValue = true;
    expectedTime += updateRxCheckTime;
    expectedTime += updateRxMainTime;
    schedulerExecute();
    EXPECT_EQ(simulatedTime, expectedTime);

    // and again.
    rxCheckReturnValue = true;
    expectedTime += updateRxCheckTime;
    expectedTime += updateRxMainTime;
    schedulerExecute();
    EXPECT_EQ(simulatedTime, expectedTime);

    // and once more off.
    rxCheckReturnValue = false;
    expectedTime += updateRxCheckTime;
    schedulerExecute();
    EXPECT_EQ(simulatedTime, expectedTime);

    // For the final test, turn on the callback but also a higher pri task. We should see the callback fire
    // but not the task execution for the RX, then on the next call we jump the time again so it we shouldn't see
    // the callback nor the execute, then on the next call we should see RX execute without the callback
    // being fired.
    rxCheckReturnValue = true;
    setTaskEnabled(TASK_GYROPID, true);
    simulatedTime = 40000;
    expectedTime = 40000;

    // Should see RX check and pid execute, we should also see the ideal execute time be !0
    schedulerExecute();
    expectedTime += updateRxCheckTime;
    expectedTime += pidLoopCheckerTime;
    EXPECT_EQ(simulatedTime, expectedTime);
    EXPECT_TRUE(cfTasks[TASK_RX].isWaitingToBeRan);
    EXPECT_GT(cfTasks[TASK_RX].lastIdealExecutionTime, 0);

    // Now we should see the check not fire and the pid execute again.
    simulatedTime = expectedTime = 50000;
    schedulerExecute();
    expectedTime += pidLoopCheckerTime;
    EXPECT_EQ(simulatedTime, expectedTime);
    EXPECT_TRUE(cfTasks[TASK_RX].isWaitingToBeRan);
    EXPECT_GT(cfTasks[TASK_RX].lastIdealExecutionTime, 0);

    // Now we should see rx execute without the check call  
    setTaskEnabled(TASK_GYROPID, false);    
    schedulerExecute();
    expectedTime += updateRxMainTime;
    EXPECT_EQ(simulatedTime, expectedTime);   
}

TEST(SchedulerUnittest, BasicPriorityTest)
{
    simulatedTime = 0;
    uint32_t expectedTime = 0;
    schedulerInit();

    // Enable a task of each pri 
    setTaskEnabled(TASK_GYROPID, true); // realtime
    setTaskEnabled(TASK_RX, true); // high
    setTaskEnabled(TASK_ACCEL, true); // medium
    setTaskEnabled(TASK_SERIAL, true); // low
    setTaskEnabled(TASK_TELEMETRY, true); // idle

    // Set all of their periods to be the same
    updateTaskExecutionPeriod(TASK_GYROPID, 3000);    
    updateTaskExecutionPeriod(TASK_RX, 3000);    
    updateTaskExecutionPeriod(TASK_ACCEL, 3000);    
    updateTaskExecutionPeriod(TASK_SERIAL, 3000);    
    updateTaskExecutionPeriod(TASK_TELEMETRY, 3000);    
    rxCheckReturnValue = false;

    // Ensure no run except for the check
    simulatedTime = expectedTime = 2999;
    schedulerExecute();
    expectedTime += updateRxCheckTime;
    EXPECT_EQ(simulatedTime, expectedTime);
    EXPECT_FALSE(cfTasks[TASK_GYROPID].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_RX].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_ACCEL].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_SERIAL].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_TELEMETRY].isWaitingToBeRan);

    // After this pass they should all be waiting to run, the rx check should have ran and the gyro
    simulatedTime = expectedTime = 3000;
    rxCheckReturnValue = true;    
    schedulerExecute();
    expectedTime += updateRxCheckTime;
    expectedTime += pidLoopCheckerTime;
    EXPECT_EQ(simulatedTime, expectedTime);
    EXPECT_FALSE(cfTasks[TASK_GYROPID].isWaitingToBeRan);
    EXPECT_TRUE(cfTasks[TASK_RX].isWaitingToBeRan);
    EXPECT_TRUE(cfTasks[TASK_ACCEL].isWaitingToBeRan);
    EXPECT_TRUE(cfTasks[TASK_SERIAL].isWaitingToBeRan);
    EXPECT_TRUE(cfTasks[TASK_TELEMETRY].isWaitingToBeRan);

    // And now keep going down the line
    schedulerExecute();
    expectedTime += updateRxMainTime;
    EXPECT_EQ(simulatedTime, expectedTime);
    EXPECT_FALSE(cfTasks[TASK_GYROPID].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_RX].isWaitingToBeRan);
    EXPECT_TRUE(cfTasks[TASK_ACCEL].isWaitingToBeRan);
    EXPECT_TRUE(cfTasks[TASK_SERIAL].isWaitingToBeRan);
    EXPECT_TRUE(cfTasks[TASK_TELEMETRY].isWaitingToBeRan);

    // And now keep going down the line
    schedulerExecute();
    expectedTime += updateAccelerometerTime;
    EXPECT_EQ(simulatedTime, expectedTime);
    EXPECT_FALSE(cfTasks[TASK_GYROPID].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_RX].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_ACCEL].isWaitingToBeRan);
    EXPECT_TRUE(cfTasks[TASK_SERIAL].isWaitingToBeRan);
    EXPECT_TRUE(cfTasks[TASK_TELEMETRY].isWaitingToBeRan);

    // And now keep going down the line
    schedulerExecute();
    expectedTime += handleSerialTime;
    EXPECT_EQ(simulatedTime, expectedTime);
    EXPECT_FALSE(cfTasks[TASK_GYROPID].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_RX].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_ACCEL].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_SERIAL].isWaitingToBeRan);
    EXPECT_TRUE(cfTasks[TASK_TELEMETRY].isWaitingToBeRan);

    // And now keep going down the line
    schedulerExecute();
    expectedTime += telemetryTime;
    EXPECT_EQ(simulatedTime, expectedTime);
    EXPECT_FALSE(cfTasks[TASK_GYROPID].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_RX].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_ACCEL].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_SERIAL].isWaitingToBeRan);
    EXPECT_FALSE(cfTasks[TASK_TELEMETRY].isWaitingToBeRan);
}

// TODO
// write test for realtime
// advance pritoirty test
// starvation test
// wirte test for pris messing with eachother.

// STUBS
extern "C" {
}

