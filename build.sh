gcc -g -o sample01_scanning sample01_scanning.c -I"/opt/ffmpeg/include" $(pkg-config --libs libavformat libavcodec libavutil);
gcc -g -o sample02_demuxing sample02_demuxing.c -I"/opt/ffmpeg/include" $(pkg-config --libs libavformat libavcodec libavutil);
gcc -g -o sample03_remuxing sample03_remuxing.c -I"/opt/ffmpeg/include" $(pkg-config --libs libavformat libavcodec libavutil);
gcc -g -o sample04_decoding sample04_decoding.c -I"/opt/ffmpeg/include" $(pkg-config --libs libavformat libavcodec libavutil);
gcc -g -o sample05_filtering sample05_filtering.c -I"/opt/ffmpeg/include" $(pkg-config --libs libavformat libavcodec libavutil libavfilter);
gcc -g -o sample06_encoding sample06_encoding.c -I"/opt/ffmpeg/include" $(pkg-config --libs libavformat libavcodec libavutil libavfilter);
