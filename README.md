# Video_Capture
A simple command line tool to save video frames in JPEG format\
This software is written in C++ using libjpeg and FFmpeg API.

# Usage
The basic usage is like
```bash
$cap input.mp4 [OPTIONS] 
```
## options
When the input video name is ```input.mp4```, it creates a folder named as ```input_frames``` and save frames into it.\
If you want to specify the output directory or make a directory with another name and save frames to it, use ```-o OUTPUT_DIRECTORY``` option.
```bash
$cap input.mp4 -o new_folder
```
If you want to specify the quality of captured images, use ```-q VALUE(INTEGER BETWEEN 0 AND 100)``` option. Default is ```75```.
```bash
$cap input.mp4 -q 80
```
