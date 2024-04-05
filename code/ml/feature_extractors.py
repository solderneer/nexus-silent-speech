import numpy as np
import scipy
from librosa.feature import mfcc, delta

def window_generator(emg, window_sample_size, window_sample_stride):
    
    # See: https://towardsdatascience.com/fast-and-robust-sliding-window-vectorization-with-numpy-3ad950ed62f5
    # Make subwindow indexes
    max_time = len(emg) - window_sample_size # How far ahead of the end we must stop
    sub_windows = (
        # expand_dims are used to convert a 1D array to 2D array in case I forget.
        np.expand_dims(np.arange(window_sample_size), 0) +
        np.expand_dims(np.arange(max_time + 1), 0).T
    )

    # Fancy indexing to make windows
    return emg[sub_windows[::window_sample_stride]]

def window_rms(a, window_size):
  a2 = np.power(a,2)
  window = np.ones((window_size, 8))/float(window_size)
  return np.sqrt(scipy.signal.convolve2d(a2, window, 'same'))

"""
Time Feature Set 1
"""
def moving_average(a, n=9):
    a = np.pad(a, ((0,0), (n // 2,n // 2), (0, 0)), 'constant', constant_values=(0, 0))
    ret = np.cumsum(a, dtype=float, axis=1)
    ret[:, n:, :] = ret[:, n:, :] - ret[:, :-n, :]
    return ret[:, n - 1:, :] / n
    
def F1(windows, n=9, sr=250):
    # Mean remove
    windows = windows - np.expand_dims(np.mean(windows, axis=1), axis=1)

    # 9 point double averaged signal    
    v = moving_average(windows, n=9)
    w = moving_average(v, n=9)
    
    # High frequency
    p = windows - w
    
    # Rectified
    r = np.abs(p)

    x_bar = np.mean(windows, axis=1)
    w_bar = np.mean(w, axis=1)
    r_bar = np.mean(r, axis=1)
    P_w = (np.sum(w**2, axis=1)) / (w.shape[1] / sr)
    P_r = (np.sum(r**2, axis=1)) / (w.shape[1] / sr)
    
    features = np.concatenate((x_bar, w_bar, r_bar, P_w, P_r), axis=1)
    return features



"""
Mean Absolute Value

axis 0: windows
axis 1: window elements
axis 2: channels
"""
def mav(emg_windows):
    return np.sum(np.abs(emg_windows), axis = 1) / emg_windows.shape[1]

"""
Waveform absolute length

axis 0: windows
axis 1: window elements
axis 2: channels
"""
def wl(emg_windows):
    emg_diff = np.diff(emg_windows, axis=1)
    return np.sum(np.abs(emg_diff), axis=1)

"""
Zero crossing

axis 0: windows
axis 1: window elements
axis 2: channels
"""
def zc(emg_windows):
    emg_diff = np.diff(emg_windows, axis=1)
    zc_windows = np.logical_and(np.abs(emg_diff) >= 20, np.diff(np.sign(emg_windows), axis=1) != 0)
    return np.sum(zc_windows, axis=1)

"""
Slope sign change

axis 0: windows
axis 1: window elements
axis 2: channels
"""
def ssc(emg_windows):
    emg_diff = np.diff(emg_windows, axis=1)
    emg_diff_diff = np.diff(emg_diff, axis=1)
    ssc_windows = np.logical_and(np.abs(emg_diff_diff) >= 20, np.diff(np.sign(emg_diff), axis=1) != 0)
    return np.sum(ssc_windows, axis=1)

""" 
F2
"""

def F2(emg, window_size, window_stride, sr=250):
    emg_windows = window_generator(emg, window_size, window_stride)

     # Time series features
    mav_features = np.expand_dims(mav(emg_windows), axis=1)
    wl_features = np.expand_dims(wl(emg_windows), axis=1)
    # zc_features = np.expand_dims(zc(emg_windows), axis=1)
    # ssc_features = np.expand_dims(ssc(emg_windows), axis=1)

    mfcc_features = None
    for window in emg_windows:
        # also try per channel, it might perform different!
        if mfcc_features is None:
            mfcc_features = mfcc(y=window.T, sr=sr, n_mfcc=6, n_mels=15, n_fft=window.shape[0], center=True)
        else:
            features = mfcc(y=window.T, sr=sr, n_mfcc=6, n_mels=15, n_fft=window.shape[0], center=True)
            mfcc_features = np.concatenate([mfcc_features, features], axis=2)
    
    mfcc_features = np.swapaxes(mfcc_features,0,2)

    features = np.concatenate((mav_features, wl_features, mfcc_features), axis=1)
    features = np.reshape(features, (features.shape[0], -1)) # Flatten channels
    return features


"""
MFCC features with delta and delta-delta
"""

def F3(emg, window_size, window_stride, sr=250):
    mfcc_features = mfcc(y=emg.T, sr=sr, n_mfcc=6, n_mels=15, n_fft=window_size, center=True)
    mfcc_delta = delta(mfcc_features)
    mfcc_delta_delta = delta(mfcc_features, order=2)

    print(mfcc_features.shape)
    print(mfcc_delta.shape)
    print(mfcc_delta_delta.shape)

    return mfcc_features