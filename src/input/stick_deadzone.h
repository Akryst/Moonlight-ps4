#pragma once
static inline short stick_deadzone(short value,int percent) {
    if(percent<=0)return value;
    if(percent>25)percent=25;
    int threshold=32768*percent/100;
    int magnitude=value<0?-(int)value:value;
    if(magnitude<=threshold)return 0;
    int scaled=(magnitude-threshold)*32767/(32768-threshold);
    return (short)(value<0?-scaled:scaled);
}
