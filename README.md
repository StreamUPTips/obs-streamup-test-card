# Test Card Plugin for OBS Studio

Turn any source into a labelled test card. Colour bars, an alignment grid, a solid colour, or your own image, with a bit of text on top so you know what you're looking at.

It's built for placeholder inputs. Say you're building an overlay on a machine that hasn't got the real capture cards plugged in yet. Drop a Colour Source in, add this filter, label it CAMERA 1, and you've got something to frame and rehearse against. Add a few more and you can build the whole multi-cam layout before a single camera is connected.

There's a shader version of this that runs on Exeldro's ShaderFilter plugin. This is the native one. Same idea, but you get real typed text in any font, a settings panel that only shows the controls that matter for the background you picked, and it can label itself from the source name. Handy if you'd rather not install ShaderFilter just for a test card.

Grab ready-made examples and the rest of the range at [StreamUP](https://streamup.tips).

## How To Use

1. Add a **Colour Source** (or any full-frame source), then add the **Test Card** filter to it.

2. Configure the filter properties:
   - **Background** - Colour Bars, Grid + Crosshair, Solid Colour, or an Image. The panel below changes to match your choice.
   - **Label Text** - Type whatever you want. CAMERA 1, NO SIGNAL, the name of the feed, anything.
   - **Use Source Name as Label** - Skip the typing and label the card with the name of the source the filter is on.
   - **Font / Text Colour / Offset** - Standard OBS font picker, a colour, and nudge the text around if the centre isn't where you want it.
   - **Label Plate** - A rounded panel behind the text so it stays readable over busy colour bars.
   - **Frame Border** - A border round the edge of the card.

## Good to Know

- **The panel adapts** - Grid controls only show when you're on the grid, image controls only show on Image, and so on. You're not scrolling past twenty settings that don't apply.
- **Real text, any font** - The shader version is stuck with a preset word and a number because shaders can't read typed text. This one takes anything you type, in any font you've got installed.
- **It replaces the source** - Whatever you drop this on gets fully covered by the card, so a plain Colour Source is the easiest thing to put it on.
- **Four backgrounds** - SMPTE-style colour bars, a grid with a centre crosshair for alignment, a flat colour, or your own PNG or JPG with Stretch, Fit or Fill.

## Build

**In-tree build:**
1. Build OBS Studio: https://obsproject.com/wiki/Install-Instructions
2. Check out this repository to `frontend/plugins/obs-streamup-test-card`
3. Add `add_subdirectory(obs-streamup-test-card)` to `frontend/plugins/CMakeLists.txt`
4. Rebuild OBS Studio

**Stand-alone build (Linux only):**
1. Make sure you have the OBS development packages installed
2. Check out this repository and run `cmake -S . -B build -DBUILD_OUT_OF_TREE=On && cmake --build build`

## Support

Built and maintained by Andi. If you're getting use out of this, consider chucking some support his way.

- [**Memberships**](https://andilippi.co.uk/pages/memberships) - Access all products and exclusive perks
- [**PayPal**](https://www.paypal.me/andilippi) - Buy me a beer
- [**Twitch**](https://www.twitch.tv/andilippi) - Come hang out and ask questions
- [**YouTube**](https://www.youtube.com/andilippi) - Tutorials on OBS and streaming
