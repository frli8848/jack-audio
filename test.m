function test(channels,device)

if nargin < 1
  channels = 2;
end

if nargin < 2
  device = 'hw:0,0';
end

fs = 44100;

x = randn(10*44100,1);
u = sin((1:128*1024)/(16*1024)*400*pi);

figure(1);
clf
plot(u(1:1000));

disp('Testing aplay');
aplay(repmat(u(:),1,channels),fs,device);

figure(2);
clf
disp('Testing arecord');
Y = arecord(10000,channels,fs,device);
plot(Y);


figure(3);
clf
disp('Testing aplayrec');
rec_channels = 2;
Y2 = aplayrec(repmat(u(:),1,channels),rec_channels,fs,device);
plot(Y2)
