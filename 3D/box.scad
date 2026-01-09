wall_thickness = 2;     // 壁厚
length = 50;            // 外长
width = 30;             // 外宽
height = 15;            // 外高
chamfer = 2;            // 倒角半径（圆角）

module base() {
    difference() {
        // 外盒 + 倒角
        minkowski() {
            cube([length - 2*chamfer, width - 2*chamfer, height - chamfer]);
            cylinder(r=chamfer, h=chamfer, $fn=50);
        }
        
        // 内腔（比外盒大一些以补偿minkowski膨胀）
        translate([wall_thickness, wall_thickness, wall_thickness])
            cube([length + 2*chamfer - 2*wall_thickness, width + 2*chamfer - 2*wall_thickness, height]);
        
        // Type-C 充电插口（前侧面，底部居中）
        translate([(length - 9)/2, -1, wall_thickness + 1])
            cube([9, wall_thickness + 2, 3.5]);
        
        // 重剑3-pin插口（右侧面，中间高度）——三个圆孔
        translate([length + 1, width/2, height/2]) 
            rotate([0, 90, 0]) {
                // 中间 B 孔
                cylinder(h = wall_thickness + 2, d = 4, $fn=50);
                // 左侧 A 孔（距中心15mm）
                translate([-15, 0, 0])
                    cylinder(h = wall_thickness + 2, d = 4, $fn=50);
                // 右侧 C 孔（距中心20mm）
                translate([20, 0, 0])
                    cylinder(h = wall_thickness + 2, d = 4, $fn=50);
            }
        
        // 第三个预留插口（后侧面，底部居中）
        translate([(length - 10)/2, width - wall_thickness -1, wall_thickness + 1])
            cube([10, wall_thickness + 2, 4]);
        
        // 重置按钮孔（左侧面，中间）
        translate([-1, width/2, height/2])
            rotate([0, 90, 0])
            cylinder(h=wall_thickness + 2, d=3.5, $fn=50);
        
        // 两个灯位孔（顶部）
        translate([length/4, width/2, height -1])
            cylinder(h=wall_thickness + 2, d=5, $fn=50);
        translate([3*length/4, width/2, height -1])
            cylinder(h=wall_thickness + 2, d=5, $fn=50);
        
        // 蜂鸣器格栅（顶部，5个小孔）
        for (i = [0:4]) {
            translate([length - 12 - i*3, width/4, height -1])
                cylinder(h=wall_thickness + 2, d=2, $fn=50);
        }
    }
}

// 生成模型
base();