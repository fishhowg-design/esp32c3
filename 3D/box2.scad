// 更新设计: 上盖和底座, 螺丝固定, ESP32固定位置
wall_thickness = 2;
length = 55;
width = 30;
height = 15;
chamfer = 2;

// ESP32尺寸
esp_length = 13.2;
esp_width = 16.6;
esp_height = 3; // 稍高以容忍

// 螺丝参数
screw_dia = 3;
screw_head_dia = 6;
screw_height = height / 2;

// 螺丝位置 (4角)
screw_pos = [[wall_thickness + 2, wall_thickness + 2],
             [length - wall_thickness - 2, wall_thickness + 2],
             [wall_thickness + 2, width - wall_thickness - 2],
             [length - wall_thickness - 2, width - wall_thickness - 2]];

// 底座
module base() {
    difference() {
        // 外形 + 倒角
        minkowski() {
            cube([length - 2*chamfer, width - 2*chamfer, height/2 - chamfer/2]);
            cylinder(r=chamfer, h=chamfer/2, $fn=50);
        }
        
        // 内腔
        translate([wall_thickness, wall_thickness, wall_thickness])
            cube([length - 2*wall_thickness, width - 2*wall_thickness, height/2]);
        
        // 螺丝孔 (底座为沉头孔)
        for (pos = screw_pos) {
            translate([pos[0], pos[1], -1])
                cylinder(h = wall_thickness + 2, d = screw_head_dia, $fn=50);
            translate([pos[0], pos[1], wall_thickness])
                cylinder(h = height/2, d = screw_dia, $fn=50);
        }
        
        // Type-C (前侧, 底座)
        translate([(length - 9)/2, -1, wall_thickness])
            cube([9, wall_thickness + 2, 3.5]);
        
        // 重剑3-pin (右侧, 跨越底座高度)
        translate([length + 1, width/2, height/4]) 
            rotate([0, 90, 0]) {
                cylinder(h = wall_thickness + 2, d = 5, $fn=50);
                translate([-15, 0, 0])
                    cylinder(h = wall_thickness + 2, d = 5, $fn=50);
                translate([20, 0, 0])
                    cylinder(h = wall_thickness + 2, d = 5, $fn=50);
            }
        
        // 第三个插口 (后侧)
        translate([(length - 10)/2, width - wall_thickness -1, wall_thickness])
            cube([10, wall_thickness + 2, 4]);
        
        // 重置按钮 (左侧)
        translate([-1, width/2, height/4])
            rotate([0, 90, 0])
            cylinder(h=wall_thickness + 2, d=3.5, $fn=50);
    }
    
    // ESP32固定位置: 底座内添加4个支柱 (假设角固定)
    esp_pos_x = wall_thickness + 5; // 位置调整
    esp_pos_y = wall_thickness + 5;
    pillar_height = esp_height + wall_thickness;
    for (dx = [0, esp_length - 2], dy = [0, esp_width - 2]) {
        translate([esp_pos_x + dx, esp_pos_y + dy, wall_thickness - 0.1])
            cylinder(h = pillar_height, d = 2, $fn=20); // 小柱子固定
    }
}

// 盖子
module lid() {
    translate([0, 0, height/2]) { // 为可视, 但实际分开打印
        difference() {
            // 外形 + 倒角
            minkowski() {
                cube([length - 2*chamfer, width - 2*chamfer, height/2 - chamfer/2]);
                cylinder(r=chamfer, h=chamfer/2, $fn=50);
            }
            
            // 内腔 (盖子内壁)
            translate([wall_thickness, wall_thickness, -wall_thickness])
                cube([length - 2*wall_thickness, width - 2*wall_thickness, height/2]);
            
            // 螺丝孔 (盖子为通孔)
            for (pos = screw_pos) {
                translate([pos[0], pos[1], -1])
                    cylinder(h = height/2 + 2, d = screw_dia, $fn=50);
            }
            
            // 两个灯孔 (盖子顶部)
            translate([length/5, width/2, height/2 -1])
                cylinder(h=wall_thickness + 2, d=5, $fn=50);
            translate([3.5*length/5, width/2, height/2 -1])
                cylinder(h=wall_thickness + 2, d=5, $fn=50);
            
            // 蜂鸣器格栅 (盖子顶部)
            for (i = [0:5]) {
                translate([length/4 + i*6, width/4, height/2 -1])
                    cylinder(h=wall_thickness + 2, d=2, $fn=50);
            }
        }
    }
}

// 生成底座和盖子 (实际打印时分开)
// base();
// lid();