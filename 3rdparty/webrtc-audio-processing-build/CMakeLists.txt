# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

if(NOT WIN32)
	message(FATAL_ERROR "The vendored WebRTC audio processing wrapper is currently Windows-only.")
endif()

set(WEBRTC_AUDIO_PROCESSING_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../webrtc-audio-processing-src")
set(WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT "${WEBRTC_AUDIO_PROCESSING_ROOT}/webrtc")

set(WEBRTC_AUDIO_PROCESSING_BASE_SOURCES
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/base/criticalsection.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/base/checks.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/base/event.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/base/platform_thread.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/base/platform_file.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/base/stringutils.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/base/thread_checker_impl.cc"
)

set(WEBRTC_AUDIO_PROCESSING_COMMON_AUDIO_SOURCES
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/resampler/push_resampler.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/resampler/push_sinc_resampler.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/resampler/resampler.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/resampler/sinc_resampler.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/resampler/sinc_resampler_sse.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/resampler/sinusoidal_linear_chirp_source.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/auto_corr_to_refl_coef.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/auto_correlation.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/complex_fft.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/complex_bit_reverse.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/copy_set_operations.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/cross_correlation.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/division_operations.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/dot_product_with_scale.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/downsample_fast.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/energy.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/filter_ar.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/filter_ar_fast_q12.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/filter_ma_fast_q12.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/get_hanning_window.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/get_scaling_square.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/ilbc_specific_functions.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/levinson_durbin.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/lpc_to_refl_coef.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/min_max_operations.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/randomization_functions.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/real_fft.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/refl_coef_to_lpc.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/resample.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/resample_48khz.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/resample_by_2.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/resample_by_2_internal.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/resample_fractional.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/spl_init.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/spl_sqrt.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/spl_sqrt_floor.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/splitting_filter.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/sqrt_of_one_minus_x_squared.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/signal_processing/vector_scaling_operations.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/vad/vad.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/vad/vad_core.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/vad/vad_filterbank.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/vad/vad_gmm.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/vad/vad_sp.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/vad/webrtc_vad.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/audio_converter.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/audio_ring_buffer.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/audio_util.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/blocker.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/channel_buffer.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/fft4g.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/fir_filter.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/fir_filter_sse.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/lapped_transform.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/real_fourier.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/real_fourier_ooura.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/ring_buffer.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/sparse_fir_filter.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/wav_file.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/wav_header.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_audio/window_generator.cc"
)

set(WEBRTC_AUDIO_PROCESSING_SYSTEM_WRAPPERS_SOURCES
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/aligned_malloc.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/cpu_features.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/event.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/file_impl.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/critical_section.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/logging.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/metrics_default.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/rw_lock.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/sleep.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/thread.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/trace_impl.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/critical_section_win.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/condition_variable.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/condition_variable_event_win.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/condition_variable_native_win.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/event_timer_win.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/rw_lock_win.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/rw_lock_generic.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/thread_win.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/system_wrappers/source/trace_win.cc"
)

set(WEBRTC_AUDIO_PROCESSING_AUDIO_CODING_SOURCES
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/arith_routines.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/arith_routines_hist.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/arith_routines_logist.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/encode_lpc_swb.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/entropy_coding.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/filter_functions.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/filterbanks.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/filterbank_tables.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/intialize.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/lpc_analysis.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/lpc_gain_swb_tables.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/lpc_shape_swb12_tables.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/lpc_shape_swb16_tables.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/lpc_tables.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/pitch_estimator.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/pitch_filter.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/pitch_gain_tables.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/pitch_lag_tables.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_coding/codecs/isac/main/source/spectrum_ar_model_tables.c"
)

set(WEBRTC_AUDIO_PROCESSING_MODULE_SOURCES
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/common_types.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/aec/aec_core.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/aec/aec_core_sse2.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/aec/aec_rdft.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/aec/aec_rdft_sse2.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/aec/aec_resampler.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/aec/echo_cancellation.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/aecm/echo_control_mobile.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/aecm/aecm_core.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/aecm/aecm_core_c.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/agc/legacy/analog_agc.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/agc/legacy/digital_agc.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/agc/agc.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/agc/agc_manager_direct.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/agc/histogram.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/agc/utility.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/beamformer/array_util.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/beamformer/covariance_matrix_generator.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/beamformer/nonlinear_beamformer.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/intelligibility/intelligibility_enhancer.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/intelligibility/intelligibility_utils.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/logging/aec_logging_file_handling.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/transient/click_annotate.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/transient/file_utils.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/transient/moving_moments.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/transient/transient_detector.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/transient/transient_suppressor.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/transient/wpd_node.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/transient/wpd_tree.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/utility/delay_estimator.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/utility/delay_estimator_wrapper.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/vad/gmm.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/vad/pitch_based_vad.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/vad/pitch_internal.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/vad/pole_zero_filter.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/vad/standalone_vad.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/vad/vad_audio_proc.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/vad/vad_circular_buffer.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/vad/voice_activity_detector.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/ns/noise_suppression.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/ns/ns_core.c"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/audio_buffer.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/audio_processing_impl.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/echo_cancellation_impl.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/echo_control_mobile_impl.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/gain_control_impl.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/high_pass_filter_impl.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/level_estimator_impl.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/noise_suppression_impl.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/rms_level.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/splitting_filter.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/processing_component.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/three_band_filter_bank.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/typing_detection.cc"
	"${WEBRTC_AUDIO_PROCESSING_WEBRTC_ROOT}/modules/audio_processing/voice_detection_impl.cc"
)

set(WEBRTC_AUDIO_PROCESSING_SOURCES
	${WEBRTC_AUDIO_PROCESSING_BASE_SOURCES}
	${WEBRTC_AUDIO_PROCESSING_COMMON_AUDIO_SOURCES}
	${WEBRTC_AUDIO_PROCESSING_SYSTEM_WRAPPERS_SOURCES}
	${WEBRTC_AUDIO_PROCESSING_AUDIO_CODING_SOURCES}
	${WEBRTC_AUDIO_PROCESSING_MODULE_SOURCES}
)

add_library(webrtc_audio_processing STATIC ${WEBRTC_AUDIO_PROCESSING_SOURCES})
set_target_properties(webrtc_audio_processing PROPERTIES UNITY_BUILD OFF)

target_include_directories(webrtc_audio_processing
	PUBLIC
		"${WEBRTC_AUDIO_PROCESSING_ROOT}"
)

target_compile_definitions(webrtc_audio_processing
	PUBLIC
		WEBRTC_AUDIO_PROCESSING_ONLY_BUILD
		WEBRTC_WIN
		_WIN32
		_WINSOCKAPI_
		NOMINMAX
		_USE_MATH_DEFINES
		WEBRTC_NS_FLOAT=1
)

target_compile_features(webrtc_audio_processing PUBLIC cxx_std_14)
target_link_libraries(webrtc_audio_processing PUBLIC winmm)
