function test(channels)

if nargin < 1
  channels = 2;
end

x = randn(10*44100,1);
u = sin((1:128*1024)/(16*1024)*400*pi);

aplay(repmat(u(:),1,10),48000,'hw:0,0');
