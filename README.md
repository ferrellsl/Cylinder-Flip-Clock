This project started decades ago on Planet Source Code at (now a dead link): http://www.Planet-Source-Code.com/vb/scripts/ShowCode.asp?txtCodeId=6348&lngWId=3  It may have also been found on the Nehe OpenGL Tutorials Site

I have removed its dependencies on glaux, fixed a logic error for 12 am and 12 pm and updated the graphics to a much higher resolution for better appearance of the numbers. I've also added shading to the top and bottom of the clock to produce a more 3D appearance and eliminated the font textures in favor of true 3D vector fonts.

This is a flip clock where the numbers rotate around a cylinder to display the correct time, much like a bicycle cylinder combination lock.

Ver. 2.0

Per-pixel shading: polished brass digits that reflect a studio room, brushed-metal drums lit as true cylinders, soft shadows under the digits and anti-aliased edges. It uses GLSL shaders fetched from the graphics driver at startup (no extra libraries); on a driver without them the clock draws as in 1.6. Press F to switch between the two looks.

<img alt="Cylinder Clock 2.0" src="screenshot-2.0.png" />


Ver. 1.6

<img width="2204" height="1080" alt="image" src="https://github.com/user-attachments/assets/1ca70227-90e1-40eb-8af4-16aded4d8b91" />


Ver. 1.4

<img width="2381" height="1014" alt="image" src="https://github.com/user-attachments/assets/a32e49c2-3361-48e3-869c-8880ba64ac49" />


Ver. 1.2

<img width="2213" height="838" alt="image" src="https://github.com/user-attachments/assets/bc4ae56c-3ef2-4abd-a746-3eb48fb91584" />



To build, open the Visual Studio Solution file and select "Build".
