#pragma once

#include <utility>
#include <vector>

#include "esphome/core/automation.h"
#include "light_effect.h"

namespace esphome {
namespace light {

inline static float random_cubic_float() {
  const float r = random_float() * 2.0f - 1.0f;
  return r * r * r;
}

/// Pulse effect.
class PulseLightEffect : public LightEffect {
 public:
  explicit PulseLightEffect(const std::string &name) : LightEffect(name) {}

  void apply() override {
    const uint32_t now = millis();
    if (now - this->last_color_change_ < this->update_interval_) {
      return;
    }
    auto call = this->state_->turn_on();
    float out = this->on_ ? this->max_brightness : this->min_brightness;
    call.set_brightness_if_supported(out);
    call.set_transition_length_if_supported(this->on_ ? this->transition_on_length_ : this->transition_off_length_);
    this->on_ = !this->on_;
    // don't tell HA every change
    call.set_publish(false);
    call.set_save(false);
    call.perform();

    this->last_color_change_ = now;
  }

  void set_transition_on_length(uint32_t transition_length) { this->transition_on_length_ = transition_length; }
  void set_transition_off_length(uint32_t transition_length) { this->transition_off_length_ = transition_length; }

  void set_update_interval(uint32_t update_interval) { this->update_interval_ = update_interval; }

  void set_min_max_brightness(float min, float max) {
    this->min_brightness = min;
    this->max_brightness = max;
  }

 protected:
  bool on_ = false;
  uint32_t last_color_change_{0};
  uint32_t transition_on_length_{};
  uint32_t transition_off_length_{};
  uint32_t update_interval_{};
  float min_brightness{0.0};
  float max_brightness{1.0};
};

/// Random effect. Sets random colors every 10 seconds and slowly transitions between them.
class RandomLightEffect : public LightEffect {
 public:
  explicit RandomLightEffect(const std::string &name) : LightEffect(name) {}

  void apply() override {
    const uint32_t now = millis();
    if (now - this->last_color_change_ < this->update_interval_) {
      return;
    }

    auto color_mode = this->state_->remote_values.get_color_mode();
    auto call = this->state_->turn_on();
    bool changed = false;
    if (color_mode & ColorCapability::RGB) {
      call.set_red(random_float());
      call.set_green(random_float());
      call.set_blue(random_float());
      changed = true;
    }
    if (color_mode & ColorCapability::COLOR_TEMPERATURE) {
      float min = this->state_->get_traits().get_min_mireds();
      float max = this->state_->get_traits().get_max_mireds();
      call.set_color_temperature(min + random_float() * (max - min));
      changed = true;
    }
    if (color_mode & ColorCapability::COLD_WARM_WHITE) {
      call.set_cold_white(random_float());
      call.set_warm_white(random_float());
      changed = true;
    }
    if (!changed) {
      // only randomize brightness if there's no colored option available
      call.set_brightness(random_float());
    }
    call.set_transition_length_if_supported(this->transition_length_);
    call.set_publish(true);
    call.set_save(false);
    call.perform();

    this->last_color_change_ = now;
  }

  void set_transition_length(uint32_t transition_length) { this->transition_length_ = transition_length; }

  void set_update_interval(uint32_t update_interval) { this->update_interval_ = update_interval; }

 protected:
  uint32_t last_color_change_{0};
  uint32_t transition_length_{};
  uint32_t update_interval_{};
};

class LambdaLightEffect : public LightEffect {
 public:
  LambdaLightEffect(const std::string &name, std::function<void(bool initial_run)> f, uint32_t update_interval)
      : LightEffect(name), f_(std::move(f)), update_interval_(update_interval) {}

  void start() override { this->initial_run_ = true; }
  void apply() override {
    const uint32_t now = millis();
    if (now - this->last_run_ >= this->update_interval_ || this->initial_run_) {
      this->last_run_ = now;
      this->f_(this->initial_run_);
      this->initial_run_ = false;
    }
  }

 protected:
  std::function<void(bool initial_run)> f_;
  uint32_t update_interval_;
  uint32_t last_run_{0};
  bool initial_run_;
};

class AutomationLightEffect : public LightEffect {
 public:
  AutomationLightEffect(const std::string &name) : LightEffect(name) {}
  void stop() override { this->trig_->stop_action(); }
  void apply() override {
    if (!this->trig_->is_action_running()) {
      this->trig_->trigger();
    }
  }
  Trigger<> *get_trig() const { return trig_; }

 protected:
  Trigger<> *trig_{new Trigger<>};
};

struct StrobeLightEffectColor {
  LightColorValues color;
  uint32_t duration;
};

class StrobeLightEffect : public LightEffect {
 public:
  explicit StrobeLightEffect(const std::string &name) : LightEffect(name) {}
  void apply() override {
    const uint32_t now = millis();
    if (now - this->last_switch_ < this->colors_[this->at_color_].duration)
      return;

    // Switch to next color
    this->at_color_ = (this->at_color_ + 1) % this->colors_.size();
    auto color = this->colors_[this->at_color_].color;

    auto call = this->state_->turn_on();
    call.from_light_color_values(this->colors_[this->at_color_].color);

    if (!color.is_on()) {
      // Don't turn the light off, otherwise the light effect will be stopped
      call.set_brightness(0.0f);
      call.set_state(true);
    }
    call.set_publish(false);
    call.set_save(false);
    call.set_transition_length_if_supported(0);
    call.perform();
    this->last_switch_ = now;
  }

  void set_colors(const std::vector<StrobeLightEffectColor> &colors) { this->colors_ = colors; }

 protected:
  std::vector<StrobeLightEffectColor> colors_;
  uint32_t last_switch_{0};
  size_t at_color_{0};
};

class FlickerLightEffect : public LightEffect {
 public:
  explicit FlickerLightEffect(const std::string &name) : LightEffect(name) {}

  void apply() override {
    LightColorValues remote = this->state_->remote_values;
    LightColorValues current = this->state_->current_values;
    LightColorValues out;
    const float alpha = this->alpha_;
    const float beta = 1.0f - alpha;
    out.set_state(true);
    out.set_brightness(remote.get_brightness() * beta + current.get_brightness() * alpha +
                       (random_cubic_float() * this->intensity_));
    out.set_red(remote.get_red() * beta + current.get_red() * alpha + (random_cubic_float() * this->intensity_));
    out.set_green(remote.get_green() * beta + current.get_green() * alpha + (random_cubic_float() * this->intensity_));
    out.set_blue(remote.get_blue() * beta + current.get_blue() * alpha + (random_cubic_float() * this->intensity_));
    out.set_white(remote.get_white() * beta + current.get_white() * alpha + (random_cubic_float() * this->intensity_));
    out.set_cold_white(remote.get_cold_white() * beta + current.get_cold_white() * alpha +
                       (random_cubic_float() * this->intensity_));
    out.set_warm_white(remote.get_warm_white() * beta + current.get_warm_white() * alpha +
                       (random_cubic_float() * this->intensity_));

    out.set_color_temperature(remote.get_color_temperature());

    auto call = this->state_->make_call();
    call.set_publish(false);
    call.set_save(false);
    call.set_transition_length_if_supported(0);
    call.from_light_color_values(out);
    call.set_state(true);
    call.perform();
  }

  void set_alpha(float alpha) { this->alpha_ = alpha; }
  void set_intensity(float intensity) { this->intensity_ = intensity; }

 protected:
  float intensity_{};
  float alpha_{};
};

class CandleLightEffect : public LightEffect {
 public:
  explicit CandleLightEffect(const std::string &name) : LightEffect(name) {}

  void start() override {
      this->need_initial_brightness_ = true;
  }

  void stop() override {
      ESP_LOGD("CandleLightEffect", "Restore Initial Brightness: %.3f",
        this->initial_brightness_);
      auto call = this->state_->make_call();
      call.set_color_mode(this->colorMode);
      call.set_brightness(this->initial_brightness_);
      call.perform();
  }

  void apply() override {

      auto light = this->state_;
      if (light->transformer_active) {
        // Something is already running.
        return;
      }

      if (this->need_initial_brightness_){
        // Only get the brightness after all transitions have finished otherwise
        // we may get zero/off.
        this->need_initial_brightness_ = false;
        this->initial_brightness_ = this->state_->current_values.get_brightness();
        this->colorMode = this->state_->current_values.get_color_mode();

        ESP_LOGD("CandleLightEffect", "Initial Brightness: %.3f   Intensity: %.3f   Flicker Depth: %.3f",
          this->initial_brightness_, this->intensity_, this->flicker_depth_);

        ESP_LOGD("CandleLightEffect", "Speed: %dms   Jitter: %d    Flicker Percent: %.3f",
          this->speed_ms_, this->speed_jitter_ms_, this->flicker_percent_);
      }

      float newBrightness;
      int transition_time_ms;

      if (flickersLeft > 0) {
        flickersLeft -= 1;

        float r = random_float();
        if (r <= 0.5){
          transition_time_ms = this->speed_ms_;
        } else {
          transition_time_ms = this->speed_ms_ + this->speed_jitter_ms_ * 0.5f;
        }

        if (is_bright_flicker_) {
          newBrightness = flickerDim;
          is_bright_flicker_ = false;
        } else {
          newBrightness = flickerBright;
          is_bright_flicker_ = true;
        }
      } else {
        float r = random_float();
        float dim_depth = 1.0f;
        // Floating point multiplication is faster than division.
        if (r <= this->flicker_percent_ * 0.10f) {
          dim_depth = 3.0f;
          state = 3;
        } else if (r <= this->flicker_percent_ * 0.5f) {
          dim_depth = 2.0f;
          state = 2;
        } else if (r <= this->flicker_percent_) {
          dim_depth = 1.0f;
          state = 1;
        } else {
          dim_depth = 0.0f;
          state = 0;
        }

        if(dim_depth <= 0.0f){
          flickerBright = this->initial_brightness_;
          flickerDim = this->initial_brightness_;
        } else {
          // Scale how dim it goes based on the initial brightness.
          // E.g. if the flicker depth is 10% and we are at 50%, then only
          // go down 5%.
          flickerBright = this->initial_brightness_ - ( dim_depth * this->intensity_ * this->initial_brightness_);
          flickerDim = flickerBright - (this->flicker_depth_ * this->initial_brightness_);
        }

        r = random_float();
        if (state == 3) {
          flickersLeft = 1;
        } else if ( r <= 0.2 ) {
          flickersLeft = 8; // 4 bright 4 dim
        } else if ( r <= 0.6 ) {
          flickersLeft = 6;
        } else if ( r <= 0.7 ) {
          flickersLeft = 4;
        } else {
          flickersLeft = 2;
        }

        int delta = abs(state - previousState);
        r = random_float();
        if (r <= 0.5){
          transition_time_ms = this->speed_ms_ * (delta < 1 ? 1 : delta);
        } else {
          transition_time_ms = (this->speed_ms_ + this->speed_jitter_ms_) * (delta < 1 ? 1 : delta);
        }

        r = random_float();
        if (r <= 0.5){
          is_bright_flicker_ = true; // start with the dimmer of the two.
        } else {
          is_bright_flicker_ = false; // start with the dimmer of the two.
        }

        newBrightness = flickerBright;
        previousState = state;
      }

      if(transition_time_ms < this->speed_ms_)
      {
        ESP_LOGW("CandleLightEffect", "Oops...the transition time is %dms, clamping to %dms",
          transition_time_ms, this->speed_ms_);
          transition_time_ms = this->speed_ms_;
      }

      ESP_LOGD("CandleLightEffect", "Brightness: %.3f    Transition Time: %dms    Short Flickers Left: %d",
        newBrightness, transition_time_ms, flickersLeft);
      auto call = this->state_->make_call();
      call.set_color_mode(this->colorMode);
      call.set_transition_length(transition_time_ms);
      call.set_brightness(newBrightness);
      // if (this->colorMode == ColorMode::RGB) {
      //   call.set_color_brightness(newBrightness);
      // } else {
      //   call.set_brightness(newBrightness);
      // }

      call.perform();
    }


  void set_intensity(float intensity) { this->intensity_ = intensity; }
  void set_flicker_depth(float depth) { this->flicker_depth_ = depth; }
  void set_flicker_percent(float percent) { this->flicker_percent_ = percent; }
  void set_flicker_speed(int speed_ms) { this->speed_ms_ = speed_ms; }
  void set_flicker_speed_jitter(int speed_jitter_ms) { this->speed_jitter_ms_ = speed_jitter_ms; }

 protected:
  float intensity_ = 0.10f;
  float flicker_percent_ = 0.8f;
  float flicker_depth_ = 0.05f;
  int speed_ms_ = 150;
  int speed_jitter_ms_ = 20;

  // State
  bool need_initial_brightness_;
  float initial_brightness_;
  ColorMode colorMode = ColorMode::UNKNOWN;
  int state = 0;
  int previousState = 0;
  int flickersLeft = 0;
  bool is_bright_flicker_ = true;
  float flickerBright = 0.95f;
  float flickerDim = 0.90f;
  float brightness_scale_ = 1.0f;
};

}  // namespace light
}  // namespace esphome
