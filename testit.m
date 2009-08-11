

Fs = 44100;
L = 10*44100;

channels = 2;

trigger_level = 0.01;
trigger_channel = 1;
trigger_frames = 0.1*Fs;
trigger_par = [trigger_level trigger_channel trigger_frames];

y = atrecord(trigger_par,L,channels,Fs,'hw:0,0');

subplot(211)
plot((0:L-1)/Fs,y(:,1));
grid on
ax = axis;
line([5 5],[ax(3) ax(4)]);

subplot(212)
plot((0:L-1)/Fs,y(:,2));
grid on
ax = axis;
line([5 5],[ax(3) ax(4)]);
