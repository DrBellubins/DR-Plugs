# Dr Plugs Plugin Suite
This repository holds the entire suite of VST plugins I have made. I make my plugins free and open source because I believe that tools used to make art should be free! Use the code from this repo however you like, as it is all under the MIT license!

## Plugin list
The plugins below are in varying states of development
### ChronoVerb
![ChronoVerb FX](https://i.imgur.com/j4x3ZJO.png)

A clustered delay/reverb effect inspired by Deelay from Sixth Sample (https://sixthsample.com/deelay/) with rich and extensive pitch modulation.

### Arp Rand
![Arp Rand MIDI FX](https://i.imgur.com/nFUbEQY.png)

A MIDI effect that takes the currently played chords, and randomly arpeggiates between them, with optional octave range for extra intrigue! Also has free mode allowing for smooth hz based rate automation!

### Range Gate
![Range Gate FX](https://i.imgur.com/dTUDOlD.png)

Though it looks simple (it kinda is), it allows for a huge breadth of audio engineering. It's an audio gate, that allows the sound to be gated if it either goes above, or below the given range. Can be used to create new and interesting transients (best used on hihats *wink* *wink*)

## Building
Make sure to use `git clone --recurse-submodules --shallow-submodules https://github.com/DrBellubins/DR-Plugs.git` when cloning.

Then checkout the submodules. Example:

`````
cd DR-Chronoverb/Libs/JUCE
git fetch --tags
git checkout 8.0.13
cd ../..
`````
