// C-Node Graph (Этап В): типы, eval, память. Хранение — project_db.c.
// Чистый dataflow float: cijcheacia графа не знает ни про ECS, ни про SQLite.
// Привязка к миру — двумя строками вызывателя (out-слот -> поле).
#ifndef ENGINE_GRAPH_H
#define ENGINE_GRAPH_H

// Типы узлов (порты фиксированы типом).
#define GN_CONST 1 // out(0)=p0
#define GN_TIME 2 // out(0)=t кадра
#define GN_ADD 3 // out=in0+in1
#define GN_MUL 4 // out=in0*in1
#define GN_SIN 5 // out=sin(in0)
#define GN_OUT 6 // in(0)->out[p0] (p0 = слот, целое >= 0)

typedef struct EngGraph EngGraph;

EngGraph *eng_graph_create(void);
void eng_graph_destroy(EngGraph *g);
// Узел (type GN_*; p0/p1 параметры: CONST значение, OUT слот). Возвращает
// id (>0) или 0 (плохой тип/слот, пул полон).
int eng_graph_add_node(EngGraph *g, int type, float p0, float p1);
// Связь out(fn.fp) -> in(tn.tp). Порты по типу (проверяются). Один драйвер
// на вход (второй линк туда же = отказ). 1 ок, 0 отказ.
int eng_graph_link(EngGraph *g, int fn, int fp, int tn, int tp);
// Прогон: t время; out[0..cap) по слотам OUT. Возвращает число слотов
// (max+1) или -1 (цикл/нет OUT/плохо). Несвязанный вход читается как 0.
int eng_graph_eval(EngGraph *g, float t, float *out, int cap);
int eng_graph_nnodes(const EngGraph *g);
int eng_graph_nlinks(const EngGraph *g);
// Клон содержимого (замена целиком, id сохраняются). 1 ок.
int eng_graph_clone(EngGraph *dst, const EngGraph *src);
// Итерация для хранения (project_db): узел/линк по индексу 0..n-1.
int eng_graph_node_at(const EngGraph *g, int idx, int *id, int *type, float *p0,
                      float *p1);
int eng_graph_link_at(const EngGraph *g, int idx, int *fn, int *fp, int *tn, int *tp);

#endif // ENGINE_GRAPH_H
