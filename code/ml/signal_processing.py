# View the isolated data filtering step
import numpy as np
import scipy.signal
import matplotlib.pyplot as plt
 
 # Testbed for signal processing strategies

def window_rms(a, window_size):
  a2 = np.power(a,2)
  window = np.ones((window_size, 8))/float(window_size)
  return np.sqrt(scipy.signal.convolve2d(a2, window, 'same'))

raw = np.load('/Users/shan/Library/CloudStorage/GoogleDrive-shan@epiph.tech/Shared drives/Tech Team/datasets/shan-calc-whisper/1704649370.npy')
print(raw.shape)
raw = raw[1:] # Remove first element which is zero
fig = plt.figure()
fig.tight_layout(pad=0)

# define some filters
sos_highpass = scipy.signal.butter(4, 0.5, 'highpass', fs=250, output='sos')
# sos_interest = scipy.signal.butter(4, [2,100], 'bandpass', fs=250, output='sos')
sos_notch_50hz = scipy.signal.butter(4, [48,52], 'bandstop', fs=250, output='sos')

mean_removed = raw - np.mean(raw, axis=0)
dc_suppressed = scipy.signal.sosfiltfilt(sos_highpass, mean_removed, axis=0, padtype='constant') # only using filt adds artifacting on the initialization
powerline_suppressed = scipy.signal.sosfiltfilt(sos_notch_50hz, dc_suppressed, axis=0, padtype='constant')

# data = scipy.signal.sosfilt(sos_interest, data, axis=0)
common = np.mean(powerline_suppressed, axis=1)
pulse_suppressed = powerline_suppressed - common[:,None]
speak_detect = window_rms(powerline_suppressed[:,:4], 5)
speak_detect = np.sum(speak_detect, axis=1)
speak_detect = np.diff(speak_detect)

plots = [("raw", raw), ("mean removed", mean_removed), ("dc_suppressed", dc_suppressed), ("powerline_suppressed", powerline_suppressed), ("pulse_suppress", pulse_suppressed), ("speak_detect", speak_detect)]
# plots = [("speak_detect", speak_detect)]

for i in range(len(plots)):
    ax = plt.subplot(len(plots),1,i + 1)
    step = plots[i][0]
    plot = plots[i][1]

    ax.set_yticks([0])
    ax.yaxis.grid(True)
    ax.tick_params(bottom=False, top=False, labelbottom=False, right=False, left=False, labelleft=False)

    ax.set_title('Step: {}'.format(step))
    lines = ax.plot(plot)
    for l,c in zip(lines, ['grey', 'mediumpurple', 'blue', 'green', 'yellow', 'orange', 'red', 'sienna']):
        l.set_color(c)

plt.show()