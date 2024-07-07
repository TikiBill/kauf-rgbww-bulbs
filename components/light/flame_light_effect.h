#pragma once

#include <utility>
#include <vector>

#include "esphome/core/automation.h"
#include "light_effect.h"

namespace esphome {
namespace light {

struct FlameEffectNumberFlickers {
  uint32_t force_at_level = 0;
  float probability = 0.1f;
  uint32_t number_flickers = 5;

  // Use int rather than unit32_t otherwise the compiler thinks the calls are ambiguous.
  FlameEffectNumberFlickers() : force_at_level(0), probability(0.1f), number_flickers(5) {}
  FlameEffectNumberFlickers(float probability, int number_flickers) : force_at_level(0), probability(probability), number_flickers(number_flickers) {}
  FlameEffectNumberFlickers(int force_at_level, int number_flickers) : force_at_level(force_at_level), probability(0.0f), number_flickers(number_flickers) {}
};


class FlameLightEffect : public LightEffect {
 public:
  explicit FlameLightEffect(const std::string &name) : LightEffect(name) {}

  void start() override {
      ESP_LOGD("FlameLightEffect", "start()");
      this->is_baseline_brightness_needed_ = true;
      this->flickers_left_ = 0;
      if (this->have_custom_flicker_intensity_) {
        ESP_LOGD("FlameLightEffect", "start()    User supplied flicker intensity: %.3f    Overall intensity: %.3f",
          this->flicker_intensity_, this->intensity_);
      } else {
        this->flicker_intensity_ = this->intensity_ / this->number_levels_ / 2.0f;
        ESP_LOGD("FlameLightEffect", "start()    Calculated flicker intensity: %.3f    Overall intensity: %.3f",
          this->flicker_intensity_, this->intensity_);
      }

      ESP_LOGD("FlameLightEffect", "start()    Speed: %dms   Jitter: %d",
        this->transition_length_ms_, this->transition_length_jitter_ms_);

      ESP_LOGD("FlameLightEffect", "start()    User supplied %d colors.", this->colors_.size());

      if(this->flicker_level_probabilities_.size() == 0){
        this->flicker_level_probabilities_ = { 0.5f, 0.3f, 0.08f };
        ESP_LOGD("FlameLightEffect", "start()    Default flicker probability.");
      } else {
        ESP_LOGD("FlameLightEffect", "start()    User supplied %d flicker probabilities.", this->flicker_level_probabilities_.size());
      }

      // Ensure the probability count matches the number of levels.
      while(this->flicker_level_probabilities_.size() < this->number_levels_)
      {
        float nextVal = this->flicker_level_probabilities_.size() > 0
          ? this->flicker_level_probabilities_[this->flicker_level_probabilities_.size() - 1] / 2.0f
          : 1.0f;
        ESP_LOGW("FlameLightEffect", "start()    Not enough flicker_level_probability values, adding %.3f.", nextVal);
        this->flicker_level_probabilities_.push_back(nextVal);
      }

      float cumulative_probability = 0.0f;
      for(int i = 0; i < this->flicker_level_probabilities_.size(); i++)
      {
        cumulative_probability += this->flicker_level_probabilities_[i];
        ESP_LOGD("FlameLightEffect", "start()    Flicker Probability %d: %.2f", i, this->flicker_level_probabilities_[i]);
      }

      if(cumulative_probability >= 1.0f) {
        ESP_LOGW("FlameLightEffect", "start()    Your cumulative flicker probability is >= 100% (%.3f) -- Zero non-flicker time.",
          cumulative_probability);
      } else {
        ESP_LOGD("FlameLightEffect", "start()   cumulative flicker probability: %.3f", cumulative_probability);
      }

      if (this->number_flickers_config_.size() == 0){
        this->number_flickers_config_ = {
          FlameEffectNumberFlickers(0.40f, 2), // The first probability does not matter, it acts as the fall-through.
          FlameEffectNumberFlickers(0.20f, 4),
          FlameEffectNumberFlickers(0.10f, 8),
          FlameEffectNumberFlickers(0.05f, 10),
          FlameEffectNumberFlickers(3, 1), // At level 3, we force a single flicker.
        };
      }

      ESP_LOGD("FlameLightEffect", "start()    Done.");
  }

  void apply() override {

    if (this->state_->is_transformer_active()) {
      // Something is already running.
      return;
    }

    if (this->have_custom_colors_ && this->colors_.size() > 0){
      // Setting the custom color is not in the start() because we
      // do not want to interrupt a running transformer.

      this->have_custom_colors_ = false; // Only set the initial color once.

      // Logging colors as integers because it is easier to take the numbers
      // to other tools (such as HTML color pickers) for comparison.
      ESP_LOGD("FlameLightEffect", "Have a %d custom colors. Color @ idx 0:  R: %d  G: %d  B: %d  (W: %d)",
        this->colors_.size(), this->colors_[0].red, this->colors_[0].green, this->colors_[0].blue, this->colors_[0].white);

      auto call = this->state_->make_call();
      call.set_publish(false);
      call.set_save(false);
      call.set_color_mode(ColorMode::RGB);
      // Commented out 'cause We will use the default transistion.
      // call.set_transition_length(transition_length_ms * 4);
      call.set_brightness(1.0f);
      call.set_rgb(this->colors_[0].red / 255.0f, this->colors_[0].green / 255.0f, this->colors_[0].blue / 255.0f);
      call.set_state(true);
      call.perform();

      return;
    }

    if (this->is_baseline_brightness_needed_){
      this->is_baseline_brightness_needed_ = false;
      this->initial_brightness_ = this->baseline_brightness_ = this->state_->current_values.get_brightness();
      this->color_mode_ = this->state_->current_values.get_color_mode();

      ESP_LOGD("FlameLightEffect", "Initial/Baseline Brightness: %.3f   Intensity: %.3f   Flicker Intensity: %.3f",
        this->baseline_brightness_, this->intensity_, this->flicker_intensity_);

      if (this->is_baseline_brightness_dim_){
        // Special case to ensure our max brightness increase can be accommodated. E.g
        // if the bulb is at 100% and we want to do a normally-dim fireplace, reduce the
        // brightness to accommodate.
        if (this->baseline_brightness_ >= 1.0f - this->intensity_)
        {
          this->baseline_brightness_ = 1.0f - this->intensity_;
          ESP_LOGD("FlameLightEffect", "Adjusting the baseline brightness to %.3f", this->baseline_brightness_);
          auto call = this->state_->make_call();
          call.set_publish(false);
          call.set_save(false);
          call.set_brightness(this->baseline_brightness_);
          call.set_state(true);
          call.perform();
        }
      }

      set_min_max_brightness();
      return; // Important! Wait for the next pass to start effects since we may have stared a transition.
    }

    float new_brightness;
    uint32_t transition_length_ms;

    if (this->flickers_left_ > 0) {
      this->flickers_left_ -= 1;

      if( this->probability_true(0.5f)){
        transition_length_ms = this->transition_length_ms_;
      } else {
        transition_length_ms = this->transition_length_ms_ + this->transition_length_jitter_ms_;
      }

      if (this->is_in_bright_flicker_state_) {
        new_brightness = this->flicker_dim_brightness_;
        this->is_in_bright_flicker_state_ = false;
      } else {
        new_brightness = this->flicker_bright_brightness_;
        this->is_in_bright_flicker_state_ = true;
      }
    } else {
      float r = random_float();

      // If we do not find a match, then use these defaults:
      float brightness_sublevel = 0.0f;
      this->flicker_state_ = 0;

      float cumulative_probability = 0.0f;
      for(int i = this->flicker_level_probabilities_.size(); i > 0; i--)
      {
        cumulative_probability += this->flicker_level_probabilities_[i - 1];
        if(r <= cumulative_probability) {
          brightness_sublevel = i;
          this->flicker_state_ = i;
          break;
        }
      }

      this->set_flicker_brightness_levels(brightness_sublevel);
      this->clamp_flicker_brightness_levels();

      this->flickers_left_ = this->determine_number_flickers();
      transition_length_ms = this->determine_transistion_length_for_new_state();
      this->is_in_bright_flicker_state_ = this->probability_true(0.5f);
      new_brightness = this->is_in_bright_flicker_state_ ? this->flicker_bright_brightness_ : this->flicker_dim_brightness_;
      this->previous_flicker_state_ = this->flicker_state_;

      ESP_LOGD("FlameLightEffect", "Random Value: %.3f  ->  Level: %.1f    Flicker State: %d    Flicker Dim: %.3f    Bright: %.3f    Flicker Count: %d",
        r, brightness_sublevel, this->flicker_state_, this->flicker_dim_brightness_, this->flicker_bright_brightness_, this->flickers_left_);
    }

    if(transition_length_ms < this->transition_length_ms_)
    {
      ESP_LOGW("FlameLightEffect", "Oops...the transition length is %dms, clamping to %dms",
        transition_length_ms, this->transition_length_ms_);
        transition_length_ms = this->transition_length_ms_;
    }

    //   ESP_LOGD("FlameLightEffect", "Brightness: %.3f    Transition Length: %dms    Short Flickers Left: %d",
    //     new_brightness, transition_length_ms, flickers_left_);
    auto call = this->state_->make_call();
    call.set_publish(false);
    call.set_save(false);
    call.set_color_mode(this->color_mode_);

    if(this->colors_.size() < 2)
    {
      // Nothing to do.
    } else if( this->colors_.size() == 2) {
      // Two colors. Auto-determine the gradient value.
      float color_fade_amount;
      if(this->is_baseline_brightness_dim_) {
        color_fade_amount = (new_brightness - this->min_brightness_) / (this->max_brightness_ - this->min_brightness_);
      } else {
        color_fade_amount = (this->max_brightness_ - new_brightness) / (this->max_brightness_ - this->min_brightness_);
      }

      if(this->use_exponential_gradient_){
        color_fade_amount = std::pow(10.0f, color_fade_amount) / 10;
      }

      Color c = this->colors_[0].gradient(this->colors_[1], color_fade_amount * 255);
      ESP_LOGD("FlameLightEffect", "Color Fade: %.1f%%    R: %d    G: %d    B: %d",
        color_fade_amount * 100.0f, c.red, c.green, c.blue);

      call.set_rgb(c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f);
    } else {
      // Assume a color per level. If there are not enough colors, use the last one.
      Color c;
      if(this->flicker_state_ < this->colors_.size()) {
        c = this->colors_[this->flicker_state_];
      } else {
        c = this->colors_[this->colors_.size() - 1];
      }
      ESP_LOGD("FlameLightEffect", "State %d Color:    R: %d    G: %d    B: %d",
        this->flicker_state_, c.red, c.green, c.blue);

      call.set_rgb(c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f);
    }

    call.set_transition_length(transition_length_ms);
    call.set_brightness(new_brightness);

    call.perform();
  }


  /*
   * The overall max intensity swing of the flicker, based on the starting brightness.
   */
  void set_intensity(float intensity) {
    if(intensity > 0.0f)
    {
      this->intensity_ = intensity;
    }
  }

  void set_flicker_intensity(float flicker_intensity) {
    if(flicker_intensity > 0.0f)
    {
      this->flicker_intensity_ = flicker_intensity; this->have_custom_flicker_intensity_ = true;
    }
  }

  void set_flicker_transition_length_ms(uint32_t length_ms) {
    if(length_ms > 0){
      this->transition_length_ms_ = length_ms;
    }
  }

  void set_flicker_transition_length_ms_jitter(uint32_t length_ms) {
    if(length_ms > 0){
      this->transition_length_jitter_ms_ = length_ms;
    }
  }

  void set_use_exponential_gradient(bool enabled){ this->use_exponential_gradient_ = enabled; }

  void set_colors(const std::vector<Color> &colors) {
    if(colors.size() > 0){
      this->colors_ = colors;
      this->have_custom_colors_ = true;
    }
  }

  void set_flicker_level_probabilities(const std::vector<float> &values) {
    if(values.size() > 0)
    {
      this->flicker_level_probabilities_= values;
    }
  }

  void set_number_flickers_config(const std::vector<FlameEffectNumberFlickers> &config){
    if(config.size() > 0)
    {
      this->number_flickers_config_ = config;
    }
  }

 protected:
  /*
   * The overall brightness swing of all levels of flicker.
   */
  float intensity_ = 0.15f;

  /*
   * The intensity of a high/low flicker, usually the overall intensity / number levels / 2.
  */
  float flicker_intensity_;
  bool have_custom_flicker_intensity_ = false;

  uint32_t transition_length_ms_ = 100;
  uint32_t transition_length_jitter_ms_ = 10;

  std::vector<float> flicker_level_probabilities_ = {}; //{ 0.8f, 0.4f, 0.08f };

  std::vector<FlameEffectNumberFlickers> number_flickers_config_ = {};

  bool use_exponential_gradient_ = true;

  float number_levels_ = 3.0f;

  bool have_custom_colors_ = false;
  std::vector<Color> colors_;

  bool is_baseline_brightness_dim_ = false;

  ColorMode color_mode_ = ColorMode::UNKNOWN;

  // State
  float initial_brightness_;
  bool is_baseline_brightness_needed_;
  float baseline_brightness_;
  uint32_t flicker_state_ = 0;
  uint32_t previous_flicker_state_ = 0;
  uint32_t flickers_left_ = 0;

  /*
   * Track if the flicker is on the bright or dim part of the
   * cycle so it can be toggled.
   */
  bool is_in_bright_flicker_state_ = true;

  /*
   * The brightness percent for the bright part of the flicker.
   * This gets changes in the code when a new flicker state is determined
   * and is scaled based on many factors.
   */
  float flicker_bright_brightness_ = 0.95f;

  /*
   * The brightness percent for the dim part of the flicker.
   * This gets changes in the code when a new flicker state is determined
   * and is scaled based on many factors.
   */
  float flicker_dim_brightness_ = 0.90f;

  //float brightness_scale_ = 1.0f;

  virtual void set_flicker_brightness_levels(float level) = 0;

  float max_brightness_ = 1.0f;
  float min_brightness_ = 0.0f;
  /*
   * Called after the baseline brightness is found. The
   * method should set max_brightness_ and min_brightness_
   * which are in turn used to determine the color of light
   * in a two-color mode based on the brightness.
   */
  virtual void set_min_max_brightness() = 0;

  /* Ensure the flicker brightness levels are not above 100% or below 0%.*/
  void clamp_flicker_brightness_levels()
  {
      if( this->flicker_bright_brightness_ > 1.0f ){ this->flicker_bright_brightness_ = 1.0f; }
      else if( this->flicker_bright_brightness_ < 0.0f ){ this->flicker_bright_brightness_ = 0.0f; }

      if( this->flicker_dim_brightness_ > 1.0f ){ this->flicker_dim_brightness_ = 1.0f; }
      else if( this->flicker_dim_brightness_ < 0.0f ){ this->flicker_dim_brightness_ = 0.0f; }
  }

  bool probability_true(float percentage)
  {
    return percentage <= random_float();
  }

  /*
   * Determine the number of flickers for the new state.
   */
  uint32_t determine_number_flickers()
  {
    if(this->number_flickers_config_.size()==0)
    {
      ESP_LOGW("FlameLightEffect", "No number of flickers config at all?");
      return 4;
    }

    for(int i = 0; i < this->number_flickers_config_.size(); i++)
    {
      if(this->flicker_state_ > 0 && this->flicker_state_ == this->number_flickers_config_[i].force_at_level)
      {
        return this->number_flickers_config_[i].number_flickers;
      }
    }

    float r = random_float();
    float cumulative_probability = 0.0f;
    for(int i = this->number_flickers_config_.size() - 1; i >= 0; i--) {
      if(this->number_flickers_config_[i].probability <= 0.0f) {
        continue;
      }

      cumulative_probability += number_flickers_config_[i].probability;
      if(r <= cumulative_probability) {
        return number_flickers_config_[i].number_flickers;
      }
    }

    // Fall through, use the first item.
    return number_flickers_config_[0].number_flickers;
  }

  uint32_t determine_transistion_length_for_new_state()
  {
    uint32_t transition_length_ms;
    uint32_t delta = abs((int)this->flicker_state_ - (int)this->previous_flicker_state_);

    if (this->probability_true(0.5)){
      transition_length_ms = this->transition_length_ms_ * (delta < 1 ? 1 : delta);
    } else {
      transition_length_ms = (this->transition_length_ms_ + this->transition_length_jitter_ms_) * (delta < 1 ? 1 : delta);
    }

    if(transition_length_ms < this->transition_length_ms_)
    {
      ESP_LOGW("FlameLightEffect", "Oops...the transition length is %dms, clamping to %dms",
        transition_length_ms, this->transition_length_ms_);

      transition_length_ms = this->transition_length_ms_;
    }

    return transition_length_ms;
  }
};

/*
 * A candle has a normal brightness and flickers dim when a breese comes by.
 */
class CandleLightEffect : public FlameLightEffect {
 public:
  explicit CandleLightEffect(const std::string &name) : FlameLightEffect(name) {}

  protected:
    void set_flicker_brightness_levels(float level)
    {
      if(this->flicker_state_ == 0){
        // No flicker.
        this->flicker_bright_brightness_ = this->baseline_brightness_;
        this->flicker_dim_brightness_ = this->baseline_brightness_;
      } else {
          this->flicker_bright_brightness_ = this->baseline_brightness_ - ( level * this->intensity_ / this->number_levels_ * this->initial_brightness_);
          this->flicker_dim_brightness_ = this->flicker_bright_brightness_ - (this->flicker_intensity_ * this->initial_brightness_);
      }
    }

    void set_min_max_brightness()
    {
    this->max_brightness_ = this->baseline_brightness_;
    this->min_brightness_ = this->baseline_brightness_ - ( this->intensity_ * this->initial_brightness_);

    ESP_LOGD("CandleLightEffect", "Min Brightness: %.3f    Max Brightness: %.3f",
      this->min_brightness_, this->max_brightness_);
    }
};

/*
 * A fireplace is glowing with embers with occasional flames brightening the room.
 * Basically the opposite of a candle.
 */
class FireplaceLightEffect : public FlameLightEffect {
 public:
  explicit FireplaceLightEffect(const std::string &name) : FlameLightEffect(name) {}

  void start() override {
    ESP_LOGW("FireplaceLightEffect", "Setting is_baseline_brightness_dim_ to true");
    this->is_baseline_brightness_dim_ = true;
    FlameLightEffect::start();
  }

protected:
  void set_flicker_brightness_levels(float level)
  {
      if(this->flicker_state_ == 0){
        // No flicker.
        this->flicker_bright_brightness_ = this->baseline_brightness_;
        this->flicker_dim_brightness_ = this->baseline_brightness_;
      } else {
        this->flicker_dim_brightness_ = this->baseline_brightness_ * (1 + (level * this->intensity_ / this->number_levels_));
        this->flicker_bright_brightness_ = this->flicker_dim_brightness_ + (this->flicker_intensity_);
      }
  }

  void set_min_max_brightness()
  {
    this->min_brightness_ = this->baseline_brightness_;
    this->max_brightness_ = this->min_brightness_ + (this->intensity_ * this->initial_brightness_);

    ESP_LOGD("FireplaceLightEffect", "Min Brightness: %.3f    Max Brightness: %.3f",
      this->min_brightness_, this->max_brightness_);
  }

};

}  // namespace light
}  // namespace esphome
