#include "slider.h"
#include "slider-vctrs.h"
#include "opts-slide.h"
#include "utils.h"
#include "segment-tree.h"

// -----------------------------------------------------------------------------

static SEXP slider_summary(SEXP x,
                           SEXP before,
                           SEXP after,
                           SEXP step,
                           SEXP complete,
                           SEXP na_rm,
                           SEXP (*fn)(SEXP x, struct slide_opts opts, bool na_rm)) {
  bool dot = false;
  struct slide_opts opts = new_slide_opts(before, after, step, complete, dot);
  bool c_na_rm = validate_na_rm(na_rm, dot);
  return fn(x, opts, c_na_rm);
}

static SEXP slide_summary(SEXP x,
                          struct slide_opts opts,
                          bool na_rm,
                          void (*fn)(const double* p_x, R_xlen_t size, const struct iter_opts* p_opts, bool na_rm, double* p_out)) {
  // Before `vec_cast()`, which may drop names
  SEXP names = PROTECT(slider_names(x, SLIDE));

  x = PROTECT(vec_cast(x, slider_shared_empty_dbl));
  const double* p_x = REAL(x);

  const R_xlen_t size = Rf_xlength(x);
  const struct iter_opts iopts = new_iter_opts(opts, size);

  SEXP out = PROTECT(slider_init(REALSXP, size));
  double* p_out = REAL(out);
  Rf_setAttrib(out, R_NamesSymbol, names);

  fn(p_x, size, &iopts, na_rm, p_out);

  UNPROTECT(3);
  return out;
}

// -----------------------------------------------------------------------------

static inline void summary_slide_loop(const struct segment_tree* p_tree,
                                      const struct iter_opts* p_opts,
                                      double* p_out) {
  double result = 0;

  R_xlen_t iter_min = p_opts->iter_min;
  R_xlen_t iter_max = p_opts->iter_max;
  R_xlen_t iter_step = p_opts->iter_step;

  R_xlen_t start = p_opts->start;
  R_xlen_t stop = p_opts->stop;

  for (R_xlen_t i = iter_min; i < iter_max; i += iter_step) {
    if (i % 1024 == 0) {
      R_CheckUserInterrupt();
    }

    R_xlen_t window_start = max(start, 0);
    R_xlen_t window_stop = min(stop + 1, p_opts->size);

    start += p_opts->start_step;
    stop += p_opts->stop_step;

    segment_tree_aggregate(p_tree, window_start, window_stop, &result);

    p_out[i] = result;
  }
}

// -----------------------------------------------------------------------------

static inline void sum_state_reset(void* p_state) {
  double* p_state_ = (double*) p_state;
  *p_state_ = 0;
}

static inline void sum_state_finalize(void* p_state, void* p_result) {
  double* p_result_ = (double*) p_result;
  double* p_state_ = (double*) p_state;
  *p_result_ = *p_state_;
  return;
}

static inline void* sum_nodes_increment(void* p_nodes) {
  return (void*) (((double*) p_nodes) + 1);
}

static inline SEXP sum_nodes_initialize(uint64_t n) {
  SEXP nodes = PROTECT(Rf_allocVector(REALSXP, n));
  double* p_nodes = REAL(nodes);

  for (uint64_t i = 0; i < n; ++i) {
    p_nodes[i] = 0;
  }

  UNPROTECT(1);
  return nodes;
}

static inline void sum_na_keep_aggregate_from_leaves(const void* p_source, uint64_t begin, uint64_t end, void* p_dest) {
  const double* p_source_ = (const double*) p_source;
  double* p_dest_ = (double*) p_dest;

  for (uint64_t i = begin; i < end; ++i) {
    *p_dest_ += p_source_[i];
  }
}

// For sum, nodes are the same type as the leaves
static inline void sum_na_keep_aggregate_from_nodes(const void* p_source, uint64_t begin, uint64_t end, void* p_dest) {
  sum_na_keep_aggregate_from_leaves(p_source, begin, end, p_dest);
}

static inline void sum_na_rm_aggregate_from_leaves(const void* p_source, uint64_t begin, uint64_t end, void* p_dest) {
  const double* p_source_ = (const double*) p_source;
  double* p_dest_ = (double*) p_dest;

  for (uint64_t i = begin; i < end; ++i) {
    const double elt = p_source_[i];

    if (!isnan(elt)) {
      *p_dest_ += elt;
    }
  }
}

static inline void sum_na_rm_aggregate_from_nodes(const void* p_source, uint64_t begin, uint64_t end, void* p_dest) {
  sum_na_rm_aggregate_from_leaves(p_source, begin, end, p_dest);
}

// -----------------------------------------------------------------------------

static SEXP slide_sum(SEXP x, struct slide_opts opts, bool na_rm);

// [[ register() ]]
SEXP slider_sum2(SEXP x, SEXP before, SEXP after, SEXP step, SEXP complete, SEXP na_rm) {
  return slider_summary(x, before, after, step, complete, na_rm, slide_sum);
}

static inline void slide_sum_impl(const double* p_x, R_xlen_t size, const struct iter_opts* p_opts, bool na_rm, double* p_out);

static SEXP slide_sum(SEXP x, struct slide_opts opts, bool na_rm) {
  return slide_summary(x, opts, na_rm, slide_sum_impl);
}

static inline void slide_sum_impl(const double* p_x, R_xlen_t size, const struct iter_opts* p_opts, bool na_rm, double* p_out) {
  int n_prot = 0;

  double state = 0;

  struct segment_tree tree = new_segment_tree(
    size,
    p_x,
    &state,
    sum_state_reset,
    sum_state_finalize,
    sum_nodes_increment,
    sum_nodes_initialize,
    na_rm ? sum_na_rm_aggregate_from_leaves : sum_na_keep_aggregate_from_leaves,
    na_rm ? sum_na_rm_aggregate_from_nodes : sum_na_keep_aggregate_from_nodes
  );
  PROTECT_SEGMENT_TREE(&tree, &n_prot);

  summary_slide_loop(&tree, p_opts, p_out);

  UNPROTECT(n_prot);
}

// -----------------------------------------------------------------------------

struct mean_state_t {
  long double sum;
  uint64_t count;
};

static inline void mean_state_reset(void* p_state) {
  struct mean_state_t* p_state_ = (struct mean_state_t*) p_state;
  p_state_->sum = 0;
  p_state_->count = 0;
}

static inline void mean_state_finalize(void* p_state, void* p_result) {
  struct mean_state_t* p_state_ = (struct mean_state_t*) p_state;
  double* p_result_ = (double*) p_result;
  *p_result_ = (double) (p_state_->sum / p_state_->count);
  return;
}

static inline void* mean_nodes_increment(void* p_nodes) {
  return (void*) (((struct mean_state_t*) p_nodes) + 1);
}

static inline SEXP mean_nodes_initialize(uint64_t n) {
  SEXP nodes = PROTECT(Rf_allocVector(RAWSXP, n * sizeof(struct mean_state_t)));
  struct mean_state_t* p_nodes = (struct mean_state_t*) RAW(nodes);

  for (uint64_t i = 0; i < n; ++i) {
    p_nodes[i].sum = 0;
    p_nodes[i].count = 0;
  }

  UNPROTECT(1);
  return nodes;
}

static inline void mean_na_keep_aggregate_from_leaves(const void* p_source, uint64_t begin, uint64_t end, void* p_dest) {
  const double* p_source_ = (const double*) p_source;
  struct mean_state_t* p_dest_ = (struct mean_state_t*) p_dest;

  for (uint64_t i = begin; i < end; ++i) {
    p_dest_->sum += p_source_[i];
    ++p_dest_->count;
  }
}

static inline void mean_na_keep_aggregate_from_nodes(const void* p_source, uint64_t begin, uint64_t end, void* p_dest) {
  const struct mean_state_t* p_source_ = (const struct mean_state_t*) p_source;
  struct mean_state_t* p_dest_ = (struct mean_state_t*) p_dest;

  for (uint64_t i = begin; i < end; ++i) {
    const struct mean_state_t source = p_source_[i];
    p_dest_->sum += source.sum;
    p_dest_->count += source.count;
  }
}

static inline void mean_na_rm_aggregate_from_leaves(const void* p_source, uint64_t begin, uint64_t end, void* p_dest) {
  const double* p_source_ = (const double*) p_source;
  struct mean_state_t* p_dest_ = (struct mean_state_t*) p_dest;

  for (uint64_t i = begin; i < end; ++i) {
    const double elt = p_source_[i];

    if (!isnan(elt)) {
      p_dest_->sum += p_source_[i];
      ++p_dest_->count;
    }
  }
}

static inline void mean_na_rm_aggregate_from_nodes(const void* p_source, uint64_t begin, uint64_t end, void* p_dest) {
  mean_na_keep_aggregate_from_nodes(p_source, begin, end, p_dest);
}

// -----------------------------------------------------------------------------

static SEXP slide_mean(SEXP x, struct slide_opts opts, bool na_rm);

// [[ register() ]]
SEXP slider_mean2(SEXP x, SEXP before, SEXP after, SEXP step, SEXP complete, SEXP na_rm) {
  return slider_summary(x, before, after, step, complete, na_rm, slide_mean);
}

static inline void slide_mean_impl(const double* p_x, R_xlen_t size, const struct iter_opts* p_opts, bool na_rm, double* p_out);

static SEXP slide_mean(SEXP x, struct slide_opts opts, bool na_rm) {
  return slide_summary(x, opts, na_rm, slide_mean_impl);
}

static inline void slide_mean_impl(const double* p_x, R_xlen_t size, const struct iter_opts* p_opts, bool na_rm, double* p_out) {
  int n_prot = 0;

  struct mean_state_t state = { .sum = 0, .count = 0 };

  struct segment_tree tree = new_segment_tree(
    size,
    p_x,
    &state,
    mean_state_reset,
    mean_state_finalize,
    mean_nodes_increment,
    mean_nodes_initialize,
    na_rm ? mean_na_rm_aggregate_from_leaves : mean_na_keep_aggregate_from_leaves,
    na_rm ? mean_na_rm_aggregate_from_nodes : mean_na_keep_aggregate_from_nodes
  );
  PROTECT_SEGMENT_TREE(&tree, &n_prot);

  summary_slide_loop(&tree, p_opts, p_out);

  UNPROTECT(n_prot);
}


