function test(channels,device)

if nargin < 1
  channels = 2;
end

if nargin < 2
  device = 'hw:0,0';
end



x = randn(10*44100,1);
u = sin((1:128*1024)/(16*1024)*400*pi);

aplay(repmat(u(:),1,channels),48000,device);
