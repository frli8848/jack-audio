%%
%% Loopback audio test script
%%
%% Connect a direct (loopback) cable between output
%% channel 3 and input channel 2 on the soundcard.

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

if (~exist('capture_channel'))
  capture_channel = input('Enter input channel number!');
end

if (~exist('play_channel'))
  play_channel = input('Enter output channel number!');
end
Y = jplayrec(single(u(:)), ['system:capture_' capture_channel],...
             ['system:playback_' play_channel]);

%%
%% 1 second data and 1 second input chirp signal
%%

figure(1);
clf;
plot(t(1:bufsize*20), Y(1:bufsize*20,1))

figure(2);
clf;
specgram(Y(:,1), bufsize, Fs_hz)

%%
%% 1 second data and 1 second input signal with a short chirp with
%% a length of 1.2 JACK periods.
%%

figure(3);
clf;

u2 = zeros(size(u,1),1);

n=1; u2((n-1)*bufsize+(1:bufsize*1.2),1) = u(1:bufsize*1.2,1);

num_skip_buffers = 0;
Y = jplayrec(single(u2(:)), ['system:capture_2'], ['system:playback_3'], num_skip_buffers);

subplot(311)
plot(t(1:bufsize*20), u2(1:bufsize*20,1))
hold on;
stem(linspace(0, 9*bufsize, 10)/Fs_hz, 0.5*ones(10,1), 'r');
title('Input signal u')
axis([0.0 0.1 -1.0 1.0]);
grid on;

subplot(312)
plot(t(1:bufsize*20), Y(1:bufsize*20,1))
hold on;
stem(linspace(0, 9*bufsize, 10)/Fs_hz, 0.5*ones(10,1), 'r');
title('Output signal y with num\_skip\_buffers=0');
axis([0.0 0.1 -1.0 1.0]);
grid on;

num_skip_buffers = 3;
Y = jplayrec(single(u2(:)), ['system:capture_2'], ['system:playback_3'], num_skip_buffers);

subplot(313)
plot(t(1:bufsize*20), Y(1:bufsize*20,1))
hold on;
stem(linspace(0, 9*bufsize, 10)/Fs_hz, 0.5*ones(10,1), 'r');
title('Output signal y with num\_skip\_buffers=3')
xlabel('t [s]');
axis([0.0 0.1 -1.0 1.0]);
grid on;

figure(4);
clf;
specgram(Y(:,1), bufsize, Fs_hz)
