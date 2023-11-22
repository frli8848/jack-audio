%%
%% Loopback audio test script
%%
%% Connect a direct (loopback) cable between output
%% channel 3 and input channel 2 on the soundcard.

s_to_ms = 1.0e3;

f1_hz = 1000;                   % Chirp start frequency
f2_hz = 24000;                  % Chirp end frequency

[Fs_hz, bufsize] = jinfo();     % Get info from the JACK server.

%% Generate a 1 second chirp input signal.
u_len = 1*Fs_hz;
u = 0.5*chirp((0:u_len-1)'/Fs_hz, f1_hz, u_len/Fs_hz, f2_hz);
u = u(:);
U = single([u u]);

t = (0:u_len-1)'/Fs_hz;
t  = t(:);

%% Setup channel numbers on the first run.

if (~exist('capture_channel'))
  capture_channel = input('Enter input channel number: ');
end

if (~exist('play_channel'))
  play_channel = input('Enter output channel number: ');
end

Y = jplayrec(single(u(:)), ['system:capture_' num2str(capture_channel)],...
             ['system:playback_' num2str(play_channel)]);

%%
%% 1 second data and 1 second input chirp signal
%%

figure(1);
clf;
plot(t(1:bufsize*20)*s_to_ms, Y(1:bufsize*20,1))
xlabel('t [ms]');

figure(2);
clf;
specgram(Y(:,1), bufsize, Fs_hz)
title('Spectogram Full chirp Sweep');

%%
%% 1 second data and 1 second input signal with a short chirp with
%% a length of 1.2 JACK periods.
%%

figure(3);
clf;

num_periods = 10;

u2 = zeros(size(u,1),1);

n=1; u2((n-1)*bufsize+(1:bufsize*1.2),1) = u(1:bufsize*1.2,1);

num_skip_buffers = 0;
Y = jplayrec(single(u2(:)), ['system:capture_2'], ['system:playback_3'], num_skip_buffers);

subplot(311)
plot(t(1:bufsize*num_periods)*s_to_ms, u2(1:bufsize*num_periods,1))
hold on;
stem(linspace(0, (num_periods-1)*bufsize, num_periods)/Fs_hz*s_to_ms, 0.5*ones(num_periods,1), 'r');
title('Input signal u')
axis([0.0 100 -1.0 1.0]);
grid on;

subplot(312)
plot(t(1:bufsize*num_periods)*s_to_ms, Y(1:bufsize*num_periods,1))
hold on;
stem(linspace(0, (num_periods-1)*bufsize, num_periods)/Fs_hz*s_to_ms, 0.5*ones(num_periods,1), 'r');
title('Output signal y with num\_skip\_buffers=0');
axis([0.0 100 -1.0 1.0]);
grid on;

num_skip_buffers = 3;
Y = jplayrec(single(u2(:)), ['system:capture_2'], ['system:playback_3'], num_skip_buffers);

subplot(313)
plot(t(1:bufsize*num_periods)*s_to_ms, Y(1:bufsize*num_periods,1))
hold on;
stem(linspace(0, (num_periods-1)*bufsize, num_periods)/Fs_hz*s_to_ms, 0.5*ones(num_periods,1), 'r');
title('Output signal y with num\_skip\_buffers=3')
axis([0.0 100 -1.0 1.0]);
grid on;

xlabel('t [ms]');

figure(4);
clf;
specgram(Y(:,1), bufsize, Fs_hz)
title('Spectogram of a Short chirp Sweep');
