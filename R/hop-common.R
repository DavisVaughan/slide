hop_common <- function(x, starts, stops, f_call, ptype, env, type, constrain, atomic) {
  x_size <- compute_size(x, type)

  check_endpoints_cannot_be_na(starts, ".starts")
  check_endpoints_cannot_be_na(stops, ".stops")

  starts <- vec_as_subscript(starts, logical = "error", character = "error", arg = ".starts")
  stops <- vec_as_subscript(stops, logical = "error", character = "error", arg = ".stops")

  size <- vec_size_common(starts, stops)
  args <- vec_recycle_common(starts, stops, .size = size)

  starts <- args[[1L]]
  stops <- args[[2L]]

  params <- list(
    type = type,
    constrain = constrain,
    atomic = atomic
  )

  .Call(hop_common_impl, x, starts, stops, f_call, ptype, env, params)
}
