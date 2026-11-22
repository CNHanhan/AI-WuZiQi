-- init_data.sql — 商城初始商品
-- 对应设计文档：04-数据库设计.md 3.3（t_shop_item：board/stone/effect/avatar）
--               03-通信协议设计.md 4.4（经典棋盘 price=0，玻璃棋子 price=500）

USE ai_wuziqi;

INSERT INTO t_shop_item (name, description, type, price, resource_path, is_hot) VALUES
('经典棋盘',   '原木纹经典棋盘，新手初始装备',        'board',  0,    ':/res/board/classic.png',   TRUE),
('玻璃棋盘',   '通透玻璃质感棋盘',                  'board',  800,  ':/res/board/glass.png',     FALSE),
('星空棋盘',   '深邃星空主题棋盘',                  'board',  1500, ':/res/board/galaxy.png',    TRUE),
('玻璃棋子',   '晶莹剔透的玻璃质感棋子',            'stone',  500,  ':/res/stone/glass.png',     TRUE),
('玛瑙棋子',   '温润玛瑙棋子，落子有声',            'stone',  1200, ':/res/stone/agate.png',     FALSE),
('玉石棋子',   '极品玉石棋子，收藏首选',            'stone',  3000, ':/res/stone/jade.png',      FALSE),
('落子流光',   '落子瞬间流光特效',                  'effect', 600,  ':/res/effect/glow.webp',    TRUE),
('五连烟花',   '五连成线时绽放烟花',                'effect', 1800, ':/res/effect/firework.webp',FALSE),
('默认头像',   '初始头像',                          'avatar', 0,    ':/res/avatar/default.png',  FALSE),
('冷咖啡',     '限定头像：冷咖啡',                  'avatar', 1000, ':/res/avatar/coldcoffee.png',TRUE),
('富江',       '限定头像：富江',                    'avatar', 2000, ':/res/avatar/fujiang.png',  FALSE);

-- 说明：价格 0 的商品供新用户直接装备；is_hot=TRUE 的在商城首页高亮展示。
