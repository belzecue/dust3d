Menu Bar
-------------

File
========

New Window
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Each window is a separate Dust3D Document. You can create a new window from here.

New
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Delete all content in the current window and get ready for your new Dust3D Document in the same window.

Open...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Open a Dust3D Document, especially a file with extension(.ds3)

Save
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Save current Dust3D Document.

Save As...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Same as Save except ask you provider a new save path.

Save All
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Save all opened window.

Export...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Export Mesh only as Wavefront Format (.obj);
Export Meshes, Skeleton, Textures and Animations as glTF Binary Format (.glb), you can use Don McCurdy's online website, glTF Viewer https://gltf-viewer.donmccurdy.com/ to check the exported result;
Export Meshes, Skeleton, Textures and Animations as Autodesk FBX Format(.fbx).

Change Reference Sheet...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Change the canvas background image, usually should be a turnaround reference sheet which has front and side view showed.

Edit
================

Add...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Switch to edit mode, get ready to add node.

Undo
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Recover the last Document Snapshot from Undo Stack.

Redo
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Reverse of Undo.

Delete
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Delete selected nodes from canvas.

Break
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Break selected edges, insert a new node in the middle of the edge.

Connect
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Connect selected two nodes.

Cut
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Copy then Delete selected nodes.

Copy
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Copy selected nodes.

Paste
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Paste nodes from Clipboard.

H Flip
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Flip selected nodes horizontally.

V Flip
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Flip selected nodes vertically.

Rotate 90D CW
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Rotate selected nodes clockwise by 90 degrees. If you can hardly edit some nodes in normal front/side view, rotate it then edit it, and rotate it back after you finish editing.

Rotate 90D CCW
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Rotate selected nodes counterclockwise by 90 degrees.

Switch XZ
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Switch selected nodes' X and Z position. if you accidentally put some parts in front view which you planned put into side view, you can select these nodes and switch the XZ components.

Align To
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Align selected nodes with center anchor globally or selected nodes' center locally. Normally, the center anchor(a Triangle) is not show up, you can turn on the Part Mirror to make it visible, then turn Part Mirror off, the center anchor would not gone once showed.

Mark As
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Mark selected nodes as specified body location explicitly, such as Leg (Start), Leg (Joint), Leg (End), and Spine.
This will help the rigging step generating more reasonable result.

Select All
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Select all nodes. Each node have two profile items, only main profile get selected.

Select Part
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Select all nodes which sit in the same part with the hovered or checked node.

Unselect All
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Unselect all nodes.

View
=====

Show Model
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Usually, you will no need to use this, because the Rendered Model always show. But if you can not find the Rendered Model and you are sure the generation is done, then maybe it goes to some weird position, you can use this menu item to reset it's position.

Show Parts List
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The Parts List Panel is a tool window, if you closed it by accident, you can show it back here.

Toggle Wireframe
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Rendered Model can be showed in two types, one with wireframe, one without.

Show Debug Dialog
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
This is for debug purpose only. It prints some useful information when debug.

Help
=====

About
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
You can check the version info of Dust3D from here.

Fork me on GitHub
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Dust3D is a totally free and open-sourced project, this bring you to the project website.

Report Issues
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
If you encounter any problem, or have any suggestion, thoughts, on Dust3D please drop it here, thanks.
