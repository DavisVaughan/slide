hop_index_common <- function(x,
                             i,
                             starts,
                             stops,
                             f_call,
                             ptype,
                             constrain,
                             atomic,
                             env,
                             type) {

  x_size <- compute_size(x, type)
  i_size <- vec_size(i)

  if (i_size != x_size) {
    stop_index_incompatible_size(i_size, x_size, ".i")
  }

  check_index_cannot_be_na(i, ".i")
  check_index_must_be_ascending(i, ".i")

  check_endpoints_cannot_be_na(starts, ".starts")
  check_endpoints_must_be_ascending(starts, ".starts")

  check_endpoints_cannot_be_na(stops, ".stops")
  check_endpoints_must_be_ascending(stops, ".stops")

  # `i` is known to be ascending,
  # so we can detect uniques very quickly with `vec_unrep()`
  unrep <- vec_unrep(i)
  i <- unrep$key
  peer_sizes <- unrep$times

  starts <- vec_cast(starts, i, x_arg = ".starts", to_arg = ".i")
  stops <- vec_cast(stops, i, x_arg = ".stops", to_arg = ".i")

  size <- vec_size_common(starts, stops)
  args <- vec_recycle_common(starts = starts, stops = stops, .size = size)

  args <- compute_combined_ranks(i = i, !!!args)

  i <- args$i
  starts <- args$starts
  stops <- args$stops

  .Call(
    hop_index_common_impl,
    x,
    i,
    starts,
    stops,
    f_call,
    ptype,
    env,
    peer_sizes,
    type,
    constrain,
    atomic,
    size
  )
}
