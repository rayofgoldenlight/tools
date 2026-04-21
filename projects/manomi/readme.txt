These are the files I used to clean and compress the data for Manomi (the Stith Thompson's Motif-Index of Folk-Literature Indexer).

At this point of time, folklore index data was sourced from here: https://ia800301.us.archive.org/18/items/Thompson2016MotifIndex/Thompson_2016_Motif-Index_djvu.txt

The work is public domain; thus, I thought it would be nice to create this.

To compress and clean the data, do:

python rc.py

Have rc.py, index.html, and raw_motifs.txt in the same folder.
Note: it will generate a folder named "dist" in the folder it is in and place the files there.

Hope this indexer inspires you to make your own.
--Ray