Taken from [EduardoWang/visual-studio-real-sense-record-and-playback-project](https://github.com/EduardoWang/visual-studio-real-sense-record-and-playback-project/blob/master/README.md)
# Create your own real sense project with Visual Studio
1. Open Visual studio, create a empty project
2. View -> other windows -> property manager
3. In property manager, right click your project, add existing property sheet
4. Go to where you installed intel realsense sdk. In my case, is C:\Program Files (x86)\Intel RealSense SDK 2.0
5. add all 3 .props file one by one

The step 3 to 5 could solve most LNK2019 error

if you are going to use window, you need to download glfw, then add path of **glfw3.lib** to **Project -> Properties -> Linker -> input -> additional dependencies**, now you are good to go. **make sure the debug bitlength (i.e. x64 or x86) matches glfw bit version you downloaded**. This will solve rest 17 LNK2019 error