gcc -g -o sample01_scaning sample01_scaning.c -I"/opt/ffmpeg/include" $(pkg-config --libs libavformat libavcodec libavutil libavfilter);
gcc -g -o sample02_demuxing sample02_demuxing.c -I"/opt/ffmpeg/include" $(pkg-config --libs libavformat libavcodec libavutil libavfilter);
gcc -g -o sample03_remuxing sample03_remuxing.c -I"/opt/ffmpeg/include" $(pkg-config --libs libavformat libavcodec libavutil libavfilter);
gcc -g -o sample04_decoding sample04_decoding.c -I"/opt/ffmpeg/include" $(pkg-config --libs libavformat libavcodec libavutil libavfilter);
